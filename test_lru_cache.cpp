// Unit tests for LRUCache.
// We keep it simple: no external test framework, just assert() calls.
// Run: ./test_lru_cache   (prints PASSED if all checks succeed)

#include "lru_cache.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <chrono>

// Helper that aborts with a message if the condition is false
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << (msg) << "  (" #cond ")\n"; \
            std::abort(); \
        } \
    } while (0)

void test_basic_set_get() {
    LRUCache cache(3);
    cache.set("a", "1");
    cache.set("b", "2");
    cache.set("c", "3");

    CHECK(cache.get("a") == "1", "get a");
    CHECK(cache.get("b") == "2", "get b");
    CHECK(cache.get("c") == "3", "get c");
    CHECK(!cache.get("x").has_value(), "missing key returns nullopt");
}

void test_lru_eviction() {
    LRUCache cache(3);
    cache.set("a", "1");
    cache.set("b", "2");
    cache.set("c", "3");

    // Access "a" so it becomes the most-recently-used.
    // After that, "b" should be the LRU candidate.
    cache.get("a");

    // Insert a 4th key — "b" should be evicted
    cache.set("d", "4");

    CHECK(!cache.get("b").has_value(), "b should have been evicted (LRU)");
    CHECK(cache.get("a") == "1", "a should still be present");
    CHECK(cache.get("c") == "3", "c should still be present");
    CHECK(cache.get("d") == "4", "d should be present");
}

void test_update_moves_to_front() {
    LRUCache cache(2);
    cache.set("a", "old");
    cache.set("b", "2");

    // Update "a" — it should move to MRU, so "b" becomes LRU
    cache.set("a", "new");

    // Insert "c" — "b" should be evicted, not "a"
    cache.set("c", "3");

    CHECK(!cache.get("b").has_value(), "b should be evicted");
    CHECK(cache.get("a") == "new",     "a should have updated value");
    CHECK(cache.get("c") == "3",       "c should be present");
}

void test_del() {
    LRUCache cache(5);
    cache.set("x", "hello");
    CHECK(cache.del("x"),              "del existing key returns true");
    CHECK(!cache.get("x").has_value(), "deleted key not findable");
    CHECK(!cache.del("x"),             "del missing key returns false");
}

void test_exists() {
    LRUCache cache(5);
    cache.set("k", "v");
    CHECK(cache.exists("k"),  "exists for present key");
    CHECK(!cache.exists("z"), "exists for missing key");
}

void test_ttl_expiry() {
    LRUCache cache(5);
    // Set with 100 ms TTL
    cache.set("tmp", "val", 100);
    CHECK(cache.get("tmp") == "val", "key present before expiry");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    CHECK(!cache.get("tmp").has_value(), "key expired after TTL");
}

void test_size() {
    LRUCache cache(10);
    CHECK(cache.size() == 0, "empty cache");
    cache.set("a", "1");
    cache.set("b", "2");
    CHECK(cache.size() == 2, "size after 2 inserts");
    cache.del("a");
    CHECK(cache.size() == 1, "size after del");
}

void test_concurrent_access() {
    // Hammer the cache from 8 threads simultaneously to check for races.
    // If there's a bug, TSAN or a crash will catch it.
    LRUCache cache(100);
    std::vector<std::thread> threads;

    for (int t = 0; t < 8; ++t) {
        threads.emplace_back([&cache, t] {
            for (int i = 0; i < 1000; ++i) {
                std::string key = "key" + std::to_string((t * 1000 + i) % 50);
                cache.set(key, "val");
                cache.get(key);
                cache.exists(key);
            }
        });
    }

    for (auto& th : threads) th.join();

    // If we got here without a crash or deadlock, concurrency is fine
    std::cout << "  [concurrency test] completed without deadlock\n";
}

int main() {
    std::cout << "Running LRUCache tests...\n";

    test_basic_set_get();
    std::cout << "  [PASS] basic set/get\n";

    test_lru_eviction();
    std::cout << "  [PASS] LRU eviction\n";

    test_update_moves_to_front();
    std::cout << "  [PASS] update moves to front\n";

    test_del();
    std::cout << "  [PASS] delete\n";

    test_exists();
    std::cout << "  [PASS] exists\n";

    test_ttl_expiry();
    std::cout << "  [PASS] TTL expiry\n";

    test_size();
    std::cout << "  [PASS] size tracking\n";

    test_concurrent_access();
    std::cout << "  [PASS] concurrent access\n";

    std::cout << "\nAll tests PASSED.\n";
    return 0;
}