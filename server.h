#pragma once

#include <string>
#include <atomic>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "lru_cache.h"
#include "thread_pool.h"

class Server {
public:
    Server(const std::string& host, int port, size_t cache_capacity, size_t num_threads);
    ~Server();

    void run();
    void stop();

private:
    std::string host_;
    int port_;
    SOCKET listen_fd_; // Windows uses SOCKETs

    LRUCache cache_;
    ThreadPool pool_;
    std::atomic<bool> running_;

    void setup_listener();
    void handle_client(SOCKET client_fd);
    void expiry_thread_fn();
    std::string dispatch(const std::string& raw);
};