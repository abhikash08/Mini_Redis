#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

// ThreadPool spins up N worker threads that pull tasks from a shared queue.
// This lets the server hand off client connections without blocking the
// epoll loop. Using a fixed pool avoids the overhead of spawning a new
// thread per connection.
//
// Usage:
//   ThreadPool pool(4);
//   pool.enqueue([]{ do_work(); });

class ThreadPool {
public:
    // Launches `num_threads` worker threads immediately.
    explicit ThreadPool(size_t num_threads);

    // Gracefully stops all workers (waits for in-flight tasks to finish).
    ~ThreadPool();

    // Adds a task to the queue. Workers pick it up as soon as one is free.
    void enqueue(std::function<void()> task);

    // Returns how many threads are in the pool.
    size_t thread_count() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> task_queue_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;

    // Set to true when the pool is shutting down; workers check this.
    std::atomic<bool> stop_;
};