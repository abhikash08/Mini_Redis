#include "thread_pool.h"
#include <stdexcept>
#include <mutex>

ThreadPool::ThreadPool(size_t num_threads) : stop_(false) {
    if (num_threads == 0) {
        throw std::invalid_argument("Thread pool must have at least one thread");
    }

    // Spin up each worker. Each one loops forever, pulling tasks from the queue.
    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this] {
            while (true) {
                std::function<void()> task;

                {
                    // Wait until there's a task or we're shutting down
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this] {
                        return stop_.load() || !task_queue_.empty();
                    });

                    // If we're stopping and there are no more tasks, exit
                    if (stop_.load() && task_queue_.empty()) return;

                    // Grab the next task off the front of the queue
                    task = std::move(task_queue_.front());
                    task_queue_.pop();
                }

                // Run the task outside the lock so other workers can grab tasks
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    // Signal all workers to stop after finishing current tasks
    stop_.store(true);
    condition_.notify_all(); // wake everyone up so they can check stop_

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (stop_.load()) {
            // Silently drop tasks submitted after shutdown
            return;
        }
        task_queue_.push(std::move(task));
    }
    // Wake up one sleeping worker to handle the new task
    condition_.notify_one();
}