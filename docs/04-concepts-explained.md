# Concepts Explained

This is the vocabulary behind Mini-Redis. For each concept, connect the definition to where the project uses it.

## Client-server networking

A server waits for connections; a client initiates them. TCP is a reliable, ordered byte-stream protocol. It does not preserve message boundaries, so one `send` can arrive in several `recv` calls or several sends can be combined. That is why `handle_client()` appends bytes until it finds a newline delimiter.

The server uses these socket steps:

1. `WSAStartup`: start the Windows socket library.
2. `socket`: create an endpoint capable of TCP communication.
3. `bind`: reserve an IP address and port, here usually `0.0.0.0:6379`.
4. `listen`: allow incoming connection requests to wait in a queue.
5. `accept`: turn one pending connection into a client socket.
6. `recv` and `send`: exchange bytes on that client socket.
7. `closesocket`: release the socket.

`AF_INET` means IPv4, `SOCK_STREAM` means a stream-oriented socket, and `IPPROTO_TCP` selects TCP.

## Socket multiplexing with `select()`

Without multiplexing, the server could block waiting on one socket and ignore all others. `select()` watches a set of sockets and returns when one or more are ready. Mini-Redis uses it to wait for either a new connection or client data without busy-waiting.

The code keeps a permanent `master_set` and makes a copy before every call because `select()` overwrites the supplied `fd_set` with only the ready sockets. The 200 ms timeout lets the loop wake periodically to notice shutdown.

`select()` is appropriate for a small teaching project. It has scale limitations because it repeatedly scans the descriptor set and has platform-dependent set-size constraints. Larger production servers often use platform event mechanisms such as IOCP on Windows, epoll on Linux, or kqueue on BSD/macOS.

## Blocking and non-blocking I/O

A blocking `recv` waits until data arrives. A non-blocking socket returns immediately if data is unavailable. The code marks listener and client sockets non-blocking with `ioctlsocket` and uses `select` to wait for readiness first.

Readiness is not a promise that all desired bytes are ready. Correct robust network software should handle partial reads, partial writes, and transient "would block" errors. This project handles command fragmentation and partial sends, but its one-command worker path treats any `recv` result less than or equal to zero as terminal. That is a reasonable limitation to acknowledge when discussing production hardening.

## Threads, concurrency, and a thread pool

A thread is an independently scheduled execution path in one process. Concurrency lets multiple client handlers run at once, especially while one waits on network I/O.

Starting a new OS thread for every connection is expensive and can exhaust resources under load. A thread pool starts a fixed number of workers once and gives them tasks from a queue:

```text
event loop -> queue task -> idle worker -> handle one client
```

This project uses:

- `std::thread` for worker and expiry threads.
- `std::queue<std::function<void()>>` as the pending task queue.
- `std::condition_variable` so idle workers sleep instead of spinning.
- `notify_one()` when a new task is available and `notify_all()` during shutdown.

The pool improves reuse and bounds the number of active worker threads. It does not make work infinite: if all workers are busy, new tasks wait in the unbounded queue. A production design might add queue limits, request timeouts, backpressure, and metrics.

## Race conditions and mutexes

A race condition occurs when multiple threads access shared mutable state without proper coordination and the result depends on timing. `std::unordered_map` and `std::list` are not safe for concurrent mutation on their own.

`LRUCache` protects both structures with one `std::mutex`. Each public cache method creates `std::lock_guard<std::mutex> lock(mutex_)`. The lock guard acquires the mutex on construction and releases it automatically at the end of scope. This protects the invariant:

> Every key in `map_` points to exactly one valid node in `usage_list_`, and every list node has a corresponding map entry.

Using one lock keeps the reasoning simple and correct. The tradeoff is that cache operations serialize under high contention. Finer-grained locking or sharding could increase throughput, but is more complex.

## Atomics

An atomic variable supports safe simple reads and writes shared between threads without using a mutex for that variable. `Server::running_` and `ThreadPool::stop_` are `std::atomic<bool>` values. One thread can set a shutdown flag while other threads observe it without a data race.

An atomic flag does not protect a compound data structure. The cache still needs a mutex because map/list updates involve many linked changes.

## LRU cache and the hash-map plus linked-list pattern

An LRU cache has a maximum number of entries. When full, it discards the entry that has not been used for the longest time.

Mini-Redis uses two structures because neither alone gives all needed operations efficiently:

| Structure | Provides | Does not provide efficiently |
| --- | --- | --- |
| `std::unordered_map` | Average O(1) lookup by key | Knowledge of oldest access |
| `std::list` | O(1) move/erase with an iterator; stable order | O(1) lookup by key |

The map stores an iterator into the list. The list is ordered MRU to LRU.

```text
front (MRU)                              back (LRU)
[account] <-> [theme] <-> [last-search]
```

When `GET theme` happens, `splice()` moves the existing `theme` node to the front without creating a new node. If a new key arrives at capacity, the back node is evicted. This gives average O(1) GET, SET, and eviction.

Example with capacity three:

```text
SET a, SET b, SET c:  c, b, a
GET a:                a, c, b
SET d:                d, a, c     (b was least recently used and removed)
```

## Time to live (TTL) and expiration

TTL means a value becomes invalid after a duration. Each entry optionally stores an absolute expiry time computed with `std::chrono::steady_clock`.

`steady_clock` is a monotonic clock: it is suitable for measuring durations because changing the wall clock does not make time move backward or forward unexpectedly.

The project uses two expiration strategies:

- **Lazy expiration:** `GET` and `EXISTS` detect expiry and erase the entry when it is accessed.
- **Active expiration:** a background thread scans every 500 ms and removes expired entries even if nobody reads them.

Using both is practical: lazy expiration is immediate on access, while active cleanup reclaims unused expired items. The sweep is O(n), so its interval and cache size are a performance tradeoff.

## `std::optional`

An optional is a value that may be absent. This project uses it in two places:

- `std::optional<std::string>` from `get`: a missing or expired key returns `std::nullopt` instead of an invented string such as `""`.
- `std::optional<time_point>` in `CacheEntry`: no value means the item never expires.

This is clearer and safer than magic sentinel values, because an empty string can be a legitimate stored value.

## RAII and exception safety

RAII means Resource Acquisition Is Initialization. A resource is acquired by an object's constructor and released by its destructor. C++ then releases it automatically when scope ends, including when an exception occurs.

Examples here:

- `std::lock_guard` releases the cache mutex automatically.
- `std::unique_lock` releases the task-queue mutex automatically.
- `Server` owns the `LRUCache` and `ThreadPool` as members; their destructors run with the server's destructor.
- `ThreadPool` joins worker threads in its destructor.

The benefit is fewer manual cleanup paths and less risk of forgotten unlocks or joins.

## Complexity notation

Big-O notation describes how running time grows with input size. O(1) means the work stays approximately constant as the number of cached keys grows. O(n) means work grows in proportion to the number of entries.

In Mini-Redis, normal cache commands are average O(1). The expiry sweep is O(n), because it must inspect every entry for expiration. Be precise in interviews: hash-map O(1) is average, not a guaranteed worst-case bound.

## Testing and benchmarking

Unit testing checks a small component in isolation. `test_lru_cache.cpp` tests cache behavior directly with assertions, including a concurrent-access smoke test.

Stress testing applies substantial concurrent load to the running server. `stress_client.py` runs a SET phase and a GET phase, then reports throughput, average latency, median latency, approximate p99 latency, and errors. Results depend on the computer, compiler, operating system, workload, and connection model, so report them as measurements from the local test environment, not universal guarantees.

## Honest limitations and strong follow-up answers

| Limitation | Why it matters | Sensible next improvement |
| --- | --- | --- |
| In-memory only | Restart loses all data. | Append-only log or snapshot persistence. |
| One command per connection | Connection setup adds overhead. | Keep connections open and parse multiple framed commands. |
| Whitespace parser | Values cannot contain spaces or binary data. | Implement RESP or length-prefixed frames. |
| Single cache mutex | Heavy concurrent access can contend. | Shard the cache by key or use lock partitioning. |
| O(n) TTL scan | Large caches can make sweeps costly. | Use a min-heap/timing wheel of expirations. |
| `select()` event loop | Limited scalability. | Use IOCP on Windows or an async I/O architecture. |
| Detached expiry thread | Shutdown ownership is harder to reason about. | Store and join the expiry thread during server shutdown. |
| No authentication or encryption | Unsafe on an untrusted network. | Bind to loopback by default, add TLS/authentication. |

Do not describe these as failures. They are deliberate scope boundaries in a learning project, and naming them shows engineering judgment.