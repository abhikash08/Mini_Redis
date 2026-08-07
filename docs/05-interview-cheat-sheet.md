# Mini-Redis Interview Cheat Sheet

Use this as the final review document after reading the other files in this folder. The answers are intentionally short enough to say aloud, but accurate to the current code.

## One-minute project summary

> Mini-Redis is a C++17, in-memory key-value server inspired by Redis. Clients connect through TCP and send simple commands such as SET, GET, DEL, EXISTS, and PING. The server uses a Windows `select()` event loop to observe sockets and hands ready client connections to a fixed thread pool. The storage layer is a thread-safe LRU cache built from an unordered map and doubly linked list, which gives average O(1) reads, writes, and evictions. Keys can have a TTL, so a background thread periodically removes expired entries while reads also remove expired keys lazily. I also wrote direct cache tests and a multithreaded Python stress client to measure throughput and latency.

## Explain The Flow

**Question: What happens when a client sends `GET user`?**

> The event loop sees that the client socket is readable through `select()`. It queues `handle_client` in the thread pool. A worker reads data until it sees a newline, then the command parser splits and normalizes the command. The dispatcher calls `LRUCache::get`. The cache locks its mutex, finds the key through the hash map, checks whether it has expired, moves it to the front of the LRU list, and returns the value. The worker formats the response, sends it, and closes the connection.

**Question: Why use a thread pool?**

> Creating a new thread for every client is expensive and can exhaust resources. A thread pool creates a fixed number of workers once and reuses them. The event loop stays responsive because it only accepts and queues connections, while workers perform request handling.

**Question: Why is the cache thread-safe?**

> Multiple worker threads can call the cache at the same time. The cache protects its hash map and linked list with one mutex, so a complete cache operation happens without another thread observing the structures halfway through an update.

## Core Design Questions

**Question: How does the LRU cache work?**

> The cache combines an `unordered_map` and a `std::list`. The map finds a key's list node in average O(1). The list keeps usage order: the front is most recently used and the back is least recently used. Every successful GET and every SET moves the node to the front. At capacity, the cache removes the node at the back.

**Question: Why not use only a hash map?**

> A hash map quickly finds a key but does not track which key was used least recently. The linked list provides that order and supports constant-time removal or movement when we already have an iterator. Together they provide both needs efficiently.

**Question: What is the time complexity?**

> GET, SET, DEL, EXISTS, and one LRU eviction are average O(1). The TTL background sweep is O(n) because it checks every cached entry. The map operations are average O(1), not guaranteed worst-case O(1).

**Question: What is TTL and how is it implemented?**

> TTL is time to live: a key becomes invalid after a duration. A cache entry stores an optional expiry time using `steady_clock`. GET and EXISTS remove expired keys when they encounter them, which is lazy expiration. A background thread also scans every 500 milliseconds and removes expired keys that nobody accesses.

**Question: Why use `steady_clock`?**

> TTL is based on elapsed duration. `steady_clock` is monotonic, so changing the computer's wall clock does not unexpectedly alter the expiry calculation.

## C++ Questions

**Question: What does `std::optional` solve here?**

> It represents a value that may be missing. `get` returns an optional string, so a missing key is `nullopt` rather than a fake value. The expiry timestamp is optional too: no timestamp means the key does not expire.

**Question: What is RAII, and where is it used?**

> RAII means resources are tied to object lifetime. `lock_guard` unlocks the mutex automatically when it leaves scope. The thread-pool destructor joins its worker threads. The Server owns the cache and pool as members, so their destructors run automatically with the Server.

**Question: What is the difference between `lock_guard` and `unique_lock` here?**

> `lock_guard` is the simple choice for cache operations that only lock and unlock. `unique_lock` is needed in the worker queue because `condition_variable::wait` temporarily releases the mutex while the worker sleeps and reacquires it when awakened.

**Question: Why are `running_` and `stop_` atomic?**

> Different threads read and write these shutdown flags. Atomics make those simple flag operations safe without using a mutex. They do not replace the cache mutex, because updating the map and list is a larger multi-step operation.

## Networking Questions

**Question: What is TCP?**

> TCP is a reliable, ordered byte-stream protocol. It gives the application a stream of bytes, not separate messages, so the server must decide where a command ends. This project uses a newline as the command delimiter.

**Question: What does `select()` do?**

> `select()` waits until one of the watched sockets is ready for reading or until a timeout expires. It lets one event-loop thread observe the listening socket and client sockets without blocking forever on any single one.

**Question: Why copy `master_set` before `select()`?**

> `select()` modifies the supplied set and leaves only ready sockets in it. The permanent set must be preserved so the server can watch all sockets again in the next loop.

**Question: Is this real Redis?**

> No. It is Redis-inspired and implements a small custom subset. It has a simple whitespace parser, one command per connection, no persistence, and no full Redis RESP compatibility.

**Question: What exact protocol limitations should you mention?**

> Keys and values cannot contain spaces because parsing splits on whitespace. Every connection handles one command and then closes. `EX` is interpreted as milliseconds here, unlike real Redis where `EX` normally means seconds. Responses use Redis-like prefixes but this is not full RESP.

## Testing Questions

**Question: How did you test the project?**

> `test_lru_cache.cpp` directly tests basic set/get, LRU eviction, updates, deletion, existence, TTL, size, and concurrent cache calls. `stress_client.py` runs many concurrent SET and GET requests against the running TCP server and reports throughput, latency, and errors.

**Question: What does the stress client measure?**

> It measures requests per second and request latency. It reports average, median, and approximate p99 latency, then verifies that each response matches the expected result.

## Trade-Offs And Improvements

**Question: What would you improve next?**

> I would first make expiry-thread shutdown explicit by owning and joining that thread instead of detaching it. Then I would support persistent client connections and a framed protocol, add persistence, and replace the O(n) TTL scan with an expiry priority queue or timing wheel. At larger scale on Windows, I would consider IOCP instead of `select()`.

**Question: What is the main scalability bottleneck?**

> The single cache mutex can serialize cache operations under heavy contention, and the thread pool has a fixed number of workers with an unbounded task queue. `select()` is also intended for smaller descriptor sets. These are reasonable trade-offs for a focused learning project, but production work would require measurements and possibly cache sharding, backpressure, and a more scalable I/O model.

**Question: What security concerns exist?**

> The server has no authentication, authorization, or encryption. It should not be exposed to an untrusted network. A safer next step is binding to loopback by default, adding authentication, and using TLS where needed.

## Improvements Or More Functionality

When asked what you would add, do not list random features. Start with correctness and safety, then protocol usability, then richer Redis-like functionality and scale.

**Question: What would you improve first in the existing implementation?**

> I would make the TTL thread owned by `Server` and join it during shutdown instead of detaching it. That gives the server a clear lifetime for every thread and avoids cleanup races. I would also add network-level tests for complete request/response behavior, because the current unit tests focus on the cache.

### 1. Better connection and protocol support

| Improvement | What it adds | Why it matters |
| --- | --- | --- |
| Persistent connections | More than one command on the same TCP connection. | Avoids connection setup and teardown for every request. |
| Proper request framing | Length-prefixed or full RESP messages. | Allows spaces, binary data, and multiple commands safely. |
| Pipelining | A client sends several commands before waiting for responses. | Improves throughput by reducing network waiting. |
| Strict command validation | Reject extra, unknown, and malformed arguments consistently. | Makes the API predictable and easier to debug. |

### 2. More user-facing commands

| Command or feature | Example | What it would do |
| --- | --- | --- |
| `TTL key` | `TTL session:1` | Report how much time remains before a key expires. |
| `EXPIRE key milliseconds` | `EXPIRE token 60000` | Add or replace the TTL of an existing key. |
| `KEYS pattern` or `SCAN` | `SCAN 0` | Discover stored keys. In production, cursor-based `SCAN` is safer than a full `KEYS` scan. |
| `INCR key` | `INCR page-views` | Atomically increase an integer value. |
| `MGET` and `MSET` | `MGET name theme` | Read or write several keys in one request. |
| Lists, sets, hashes | `LPUSH queue job1` | Support richer data types, like real Redis. |
| `INFO` or `STATS` | `INFO` | Show cache size, evictions, request counts, and worker metrics. |

### 3. Reliability and storage

| Improvement | What it means |
| --- | --- |
| Persistence | Save commands to an append-only log or periodically write a snapshot so data can survive restart. |
| Graceful shutdown | Stop accepting connections, drain or cancel queued tasks by policy, join every owned thread, then release sockets. |
| Structured logging | Record connections, commands, errors, eviction counts, and shutdown events in a useful format. |
| Configuration | Allow limits and options through a config file or command-line flags, including host binding and maximum queue size. |
| Test coverage | Add parser tests, integration tests using real sockets, malformed-input tests, and sanitizer/race-detector runs. |

### 4. Performance and scalability

| Improvement | Reason |
| --- | --- |
| Bounded task queue and backpressure | Prevent unlimited queued client work from exhausting memory during overload. |
| Cache sharding | Use multiple locks for different key partitions to reduce contention on one cache mutex. |
| Expiry priority queue or timing wheel | Avoid scanning every key every 500 ms to find expired entries. |
| IOCP on Windows | Scale socket I/O beyond the small-set model of `select()`. |
| Benchmark metrics | Measure queue wait time, worker utilization, eviction rate, memory use, and tail latency before optimizing. |

**Question: Which feature would you implement after the basics, and why?**

> I would implement persistent connections with a proper framed protocol. It improves the server's real usability and also fixes the current limitation that values cannot contain spaces. After that, I would add `TTL`, `EXPIRE`, and `INFO`, because they make existing TTL and cache behavior visible to a client.

## Final Reminders

- Say "average O(1)" for hash-map operations, not unconditional O(1).
- Say "Redis-inspired," not "a Redis clone."
- Explain the reason behind each structure: event loop for socket readiness, pool for bounded reusable concurrency, map plus list for LRU, mutex for cache consistency, and TTL sweeper for cleanup.
- Be honest about scope limits, then state the next engineering improvement. That shows stronger judgment than claiming the project is production-ready.

For detail behind any answer, return to [01-project-introduction.md](01-project-introduction.md), [02-project-workflow.md](02-project-workflow.md), [03-code-walkthrough.md](03-code-walkthrough.md), and [04-concepts-explained.md](04-concepts-explained.md).