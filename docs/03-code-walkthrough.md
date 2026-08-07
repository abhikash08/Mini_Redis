# Code Walkthrough

Use this as a file-by-file guide when an interviewer asks, "What does this code do?" Start with the high-level responsibility, then explain the key methods.

## `main.cpp`: program entry point

`main()` is the composition root: it chooses configuration, creates the server, and starts it.

- `g_server` is a global non-owning pointer. The signal handler needs it because a C-style signal handler cannot receive the local `server` variable as an argument.
- `signal_handler()` logs the signal and calls `stop()` if the server exists.
- The defaults are port `6379`, capacity `10000`, and four workers. `std::atoi` converts optional command-line arguments.
- `Server server(...)` is inside `try`; constructor failures such as Winsock initialization failure are caught and displayed as fatal errors.
- `server.run()` blocks until `stop()` changes the running flag.

Interview point: `main` owns the `Server` object, so its destructor runs automatically when `main` ends. This is RAII: resources are released by object lifetime.

## `server.h` and `server.cpp`: network and command layer

The `Server` class owns configuration, the listening socket, the cache, the worker pool, and the `running_` atomic flag.

### Constructor and destructor

- The constructor initializer list creates `cache_` and `pool_` before the body runs. It then calls `WSAStartup` to initialize the Windows networking library.
- The destructor closes the listening socket if it is valid, then calls `WSACleanup`.

### `setup_listener()`

This method prepares the TCP server socket:

1. `socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)` creates an IPv4 TCP socket.
2. `SO_REUSEADDR` asks the OS to permit rebinding the port in common restart situations.
3. `ioctlsocket(..., FIONBIO, ...)` makes the listening and accepted client sockets non-blocking.
4. `sockaddr_in` stores the IPv4 address and port. `htons` converts the port to network byte order; `inet_pton` converts the printable address to binary form.
5. `bind` attaches the socket to the address, and `listen` starts accepting connection requests.

### `run()`

`run()` is the event-loop method.

- It starts the listener and sets `running_` to true.
- It launches the TTL sweep function on a background thread.
- `master_set` records sockets the event loop wants to watch for reads.
- Before each `select`, the code copies `master_set` into `read_fds` because `select()` modifies the supplied set to contain only ready sockets.
- When the listening socket is ready, `accept()` obtains a new client socket and adds it to `master_set`.
- When a client socket is ready, the loop removes it from the set and queues a lambda that calls `handle_client(fd)` on a worker.

### `handle_client()`

This is the worker-side connection handler.

- `recv` reads network bytes into a fixed 4,096-byte buffer.
- The method appends received chunks to `request_buf` because TCP is a byte stream: one logical message may arrive in multiple reads.
- It waits for a newline, extracts exactly one command, dispatches it, and loops around `send` to cope with partial sends.
- It closes the connection after the one response.

### `dispatch()`

`dispatch()` is the bridge from text commands to cache operations.

- It asks `CommandParser` for an optional parsed command. Empty input receives an error.
- It checks `cmd.name` and validates the required arguments before calling `cache_.set`, `get`, `del`, or `exists`.
- It converts results to the project's text protocol, such as `+OK\r\n`, `:1\r\n`, or `$-1\r\n`.
- For `SET ... EX n`, it parses `n` with `std::stoi`; an invalid integer returns an error rather than crashing.

### `expiry_thread_fn()`

While the server is running, this method sleeps for 500 milliseconds, then calls `cache_.evict_expired()`. Sleeping avoids wasting CPU while still cleaning expired keys periodically.

## `command_parser.h` and `command_parser.cpp`: input normalization

`ParsedCommand` contains `name` and a `std::vector` of the remaining tokens.

- `tokenize()` uses `std::istringstream >> token`, which naturally ignores repeated spaces and splits at all whitespace.
- `parse()` removes trailing CR/LF characters so both Windows-style `\r\n` and Unix-style `\n` requests work.
- `std::transform(..., ::toupper)` turns the command name into uppercase. Therefore `get key` and `GET key` behave the same way.
- Empty input returns `std::nullopt`, an explicit way to represent "no parsed command."

Constraint to say aloud: tokenization is deliberately simple, so it does not support quoting or spaces inside a value.

## `lru_cache.h` and `lru_cache.cpp`: storage, eviction, and TTL

### Data model

`CacheEntry` has:

- `value`: the string stored for a key.
- `expires_at`: an `std::optional<steady_clock::time_point>`. An empty optional means no TTL.

`LRUCache` keeps the same key in two coordinated structures:

- `usage_list_`: a doubly linked list of `(key, CacheEntry)` nodes. Front means most recently used (MRU); back means least recently used (LRU).
- `map_`: an `unordered_map` from key to that key's list iterator.

The map finds a node quickly, while the iterator lets the code move or erase the exact list node in constant time.

### Helpers

- `is_expired_locked()` checks whether an optional expiry exists and lies before the current `steady_clock` time.
- `evict_lru_locked()` takes the list's last node, erases its key from the map, then removes the list node.
- Names ending in `_locked` document the precondition that the caller already holds `mutex_`.

### Public methods

- `get`: find, check expiry, lazily erase if expired, otherwise move the node to the front with `list::splice`, and return the value.
- `set`: create the new entry, replace and promote an existing key, or evict one old entry before adding a new key at the front.
- `del`: remove the list node through the iterator found in the map, then remove the map entry.
- `exists`: is like a presence check, but it also deletes and rejects expired entries.
- `size`: reports `map_.size()` under the mutex.
- `evict_expired`: scans the list, collects expired keys, then deletes them. Collecting first avoids invalidating the loop iterator while walking the list.

Every public cache method uses `std::lock_guard<std::mutex>`, so the mutex unlocks automatically when the method returns, even through an early return.

## `thread_pool.h` and `thread_pool.cpp`: reusable workers

The pool contains a vector of worker threads and one shared FIFO task queue.

- Its constructor rejects zero workers, then starts exactly `num_threads` worker loops.
- Each worker takes a `unique_lock`, waits on a condition variable until either shutdown begins or a task arrives, removes one task, releases the lock, and executes the task.
- `enqueue()` locks only long enough to add a task, then calls `notify_one()` to wake one worker.
- The destructor sets the stop flag, wakes all workers, and `join`s them. A worker exits only once shutdown is requested and the queue has been drained.

The important design choice is running `task()` after releasing `queue_mutex_`. Otherwise one slow client would prevent every worker from fetching new work.

## `stress_client.py`: concurrent benchmark client

The Python client is not part of the server; it measures it.

- `send_command()` creates a new TCP connection, sends one command plus CRLF, reads to a full response line, and closes. This exactly matches the server's one-command-per-connection design.
- `SO_LINGER` with `(1, 0)` forces a reset-like close to reduce Windows ephemeral-port exhaustion during rapid connection churn. This is a benchmark workaround, not normal application behavior.
- `set_worker` and `get_worker` measure individual request latency and record failed responses.
- `run_phase()` splits the total work among Python threads, waits for all of them, and reports requests per second plus average, median, and approximate p99 latency.

## `test_lru_cache.cpp`: direct cache tests

This executable uses a custom `CHECK` macro instead of a test framework. It covers basic storage, LRU order, update promotion, deletion, existence, TTL expiration, size reporting, and concurrent cache calls from eight threads. It validates the cache in isolation; it does not test TCP behavior or the parser.

Build it separately with:

```powershell
g++ -std=c++17 test_lru_cache.cpp lru_cache.cpp -o test_lru_cache -pthread
.\test_lru_cache.exe
```