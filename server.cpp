#include "server.h"
#include "command_parser.h"

#include <iostream>
#include <sstream>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <algorithm>

// Windows networking headers
#include <winsock2.h>
#include <ws2tcpip.h>

static constexpr int EXPIRY_SWEEP_MS = 500;
static constexpr int READ_BUF_SIZE = 4096;

static void set_nonblocking(SOCKET fd) {
    u_long mode = 1;
    ioctlsocket(fd, FIONBIO, &mode);
}

Server::Server(const std::string& host, int port, size_t cache_capacity, size_t num_threads)
    : host_(host), port_(port), listen_fd_(INVALID_SOCKET), cache_(cache_capacity), pool_(num_threads), running_(false) {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
}

Server::~Server() {
    if (listen_fd_ != INVALID_SOCKET) closesocket(listen_fd_);
    WSACleanup();
}

void Server::stop() { running_.store(false); }

void Server::setup_listener() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd_ == INVALID_SOCKET) throw std::runtime_error("socket() failed");

    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
    set_nonblocking(listen_fd_);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port_);
    inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        throw std::runtime_error("bind() failed");
    }
    if (listen(listen_fd_, SOMAXCONN) == SOCKET_ERROR) {
        throw std::runtime_error("listen() failed");
    }
    std::cout << "[server] Listening on " << host_ << ":" << port_ << " (Windows select)\n";
}

void Server::run() {
    setup_listener();
    running_.store(true);

    std::thread expiry_thread([this] { expiry_thread_fn(); });
    expiry_thread.detach();

    std::cout << "[server] Event loop started (thread pool: " << pool_.thread_count() << " workers)\n";

    fd_set master_set;
    FD_ZERO(&master_set);
    FD_SET(listen_fd_, &master_set);

    while (running_.load()) {
        fd_set read_fds = master_set;
        timeval timeout{0, 200000}; // 200 ms timeout

        int activity = select(0, &read_fds, nullptr, nullptr, &timeout);
        if (activity == SOCKET_ERROR) break;
        if (activity == 0) continue; // timeout

        if (FD_ISSET(listen_fd_, &read_fds)) {
            SOCKET client_fd = accept(listen_fd_, nullptr, nullptr);
            if (client_fd != INVALID_SOCKET) {
                set_nonblocking(client_fd);
                FD_SET(client_fd, &master_set);
            }
        }

        for (unsigned int i = 0; i < master_set.fd_count; ++i) {
            SOCKET fd = master_set.fd_array[i];
            if (fd != listen_fd_ && FD_ISSET(fd, &read_fds)) {
                FD_CLR(fd, &master_set); // Hand off to thread pool
                pool_.enqueue([this, fd] {
                    handle_client(fd);
                });
            }
        }
    }
    std::cout << "[server] Shutting down.\n";
}

void Server::handle_client(SOCKET client_fd) {
    char buf[READ_BUF_SIZE];
    std::string request_buf;

    while (true) {
        int bytes_read = recv(client_fd, buf, sizeof(buf) - 1, 0);
        if (bytes_read <= 0) {
            closesocket(client_fd);
            return;
        }

        buf[bytes_read] = '\0';
        request_buf += buf;

        size_t newline = request_buf.find('\n');
        if (newline == std::string::npos) continue;

        std::string command = request_buf.substr(0, newline + 1);
        std::string response = dispatch(command);

        size_t total_sent = 0;
        while (total_sent < response.size()) {
            int sent = send(client_fd, response.c_str() + total_sent, response.size() - total_sent, 0);
            if (sent <= 0) break;
            total_sent += sent;
        }
        closesocket(client_fd);
        return;
    }
}

std::string Server::dispatch(const std::string& raw) {
    auto maybe_cmd = CommandParser::parse(raw);
    if (!maybe_cmd.has_value()) return "-ERR empty command\r\n";

    const auto& cmd  = maybe_cmd.value();
    const auto& args = cmd.args;

    if (cmd.name == "PING") return "+PONG\r\n";
    if (cmd.name == "SET") {
        if (args.size() < 2) return "-ERR SET requires key and value\r\n";
        int ttl_ms = 0;
        if (args.size() >= 4) {
            std::string flag = args[2];
            std::transform(flag.begin(), flag.end(), flag.begin(), ::toupper);
            if (flag == "EX") {
                try { ttl_ms = std::stoi(args[3]); }
                catch (...) { return "-ERR EX value must be an integer\r\n"; }
            }
        }
        cache_.set(args[0], args[1], ttl_ms);
        return "+OK\r\n";
    }
    if (cmd.name == "GET") {
        if (args.empty()) return "-ERR GET requires a key\r\n";
        auto val = cache_.get(args[0]);
        if (!val.has_value()) return "$-1\r\n";
        return "$" + val.value() + "\r\n";
    }
    if (cmd.name == "DEL") {
        if (args.empty()) return "-ERR DEL requires a key\r\n";
        return cache_.del(args[0]) ? ":1\r\n" : ":0\r\n";
    }
    if (cmd.name == "EXISTS") {
        if (args.empty()) return "-ERR EXISTS requires a key\r\n";
        return cache_.exists(args[0]) ? ":1\r\n" : ":0\r\n";
    }
    return "-ERR unknown command '" + cmd.name + "'\r\n";
}

void Server::expiry_thread_fn() {
    while (running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(EXPIRY_SWEEP_MS));
        cache_.evict_expired();
    }
}