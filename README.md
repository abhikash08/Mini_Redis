Mini Redis
A high-performance, multithreaded, in-memory key-value store written in C++17. This project implements core Redis-like functionality from scratch, including a custom TCP server, Least Recently Used (LRU) cache eviction, and thread-safe data access.

1. Core Features
1. In-Memory Storage: Fast O(1) key-value lookups using standard hash maps.
2. LRU Eviction Policy: Manages memory limits by automatically evicting the least recently accessed items using a doubly-linked list paired with a hash map.
3. Custom TCP Server: Built using raw sockets and select() for cross-platform (Windows) multiplexing, handling multiple concurrent client connections.
4. Concurrency: Implements a custom thread pool using std::thread, std::mutex, and condition variables to safely process incoming requests without blocking the main event loop.
5. Time-to-Live (TTL): Background thread sweeps and removes expired keys automatically.

2. Architecture
The system is divided into three main layers:
1. Network Layer: A non-blocking TCP listener that queues incoming client requests.
2. Worker Pool: A predefined number of threads that pull tasks from the network queue and execute parsed commands.
3. Storage Layer: A thread-safe LRU cache protected by lock guards to prevent race conditions during concurrent reads and writes.

3. Performance and Stress Testing
The server was benchmarked using a custom multithreaded Python client firing 100,000 total requests across 16 concurrent threads.
* System Bottleneck & Resolution:
  * During initial stress testing, the volume of rapid connections caused OS-level TCP port exhaustion (WinError 10048), outpacing the OS's ability to clear the TIME_WAIT state. 
  * This was resolved by configuring socket linger options (SO_LINGER) to forcefully reset connections (RST) and instantly recycle ephemeral ports.
* Benchmarks (Local Environment):
  * SET Throughput: ~6,700 requests/second
  * GET Throughput: ~4,800 requests/second
  * Average Latency: < 4ms

4. Build and Run Instructions

Prerequisites:
1. A modern C++ compiler supporting C++17 (e.g., MSYS2/MinGW-w64 on Windows).
2. Python 3.x (for stress testing).

Execution Steps:
1. Compiling the Server: Compile the source code using g++. Note the inclusion of -lws2_32 for Windows socket support.
   g++ -std=c++17 main.cpp server.cpp command_parser.cpp thread_pool.cpp lru_cache.cpp -o mini_redis -lws2_32 -pthread

2. Running the Server:
   .\mini_redis.exe

3. Running the Stress Test: In a separate terminal window, execute the Python test client:
   python stress_client.py --requests 50000 --threads 16
