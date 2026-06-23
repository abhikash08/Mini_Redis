#include "server.h"

#include <iostream>
#include <csignal>
#include <stdexcept>
#include <cstdlib>

// Global pointer so the signal handler can reach the server instance.
// Using a raw pointer here is fine since main() owns the lifetime.
static Server* g_server = nullptr;

void signal_handler(int /*sig*/) {
    std::cout << "\n[main] Caught signal, stopping server...\n";
    if (g_server) g_server->stop();
}

int main(int argc, char* argv[]) {
    // Simple command-line config: ./mini-redis [port] [cache_size] [threads]
    int    port           = 6379;
    size_t cache_capacity = 10000;
    size_t num_threads    = 4;

    if (argc >= 2) port           = std::atoi(argv[1]);
    if (argc >= 3) cache_capacity = std::atoi(argv[2]);
    if (argc >= 4) num_threads    = std::atoi(argv[3]);

    std::cout << "=== Mini-Redis ===\n"
              << "Port: "     << port           << "\n"
              << "Cache cap: "<< cache_capacity << " keys\n"
              << "Workers: "  << num_threads    << " threads\n\n";

    try {
        Server server("0.0.0.0", port, cache_capacity, num_threads);
        g_server = &server;

        // Register Ctrl-C / kill signal handlers for clean shutdown
        std::signal(SIGINT,  signal_handler);
        std::signal(SIGTERM, signal_handler);

        server.run(); // blocks until stop() is called
    } catch (const std::exception& e) {
        std::cerr << "[fatal] " << e.what() << "\n";
        return 1;
    }

    return 0;
}