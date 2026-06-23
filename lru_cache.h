#pragma once
#include <vector>
#include <unordered_map>
#include <list>
#include <string>
#include <optional>
#include <mutex>
#include <chrono>

// LRUCache implements a thread-safe, fixed-capacity key-value store
// with Least Recently Used eviction. When the cache is full, the entry
// that hasn't been accessed for the longest time gets removed.
//
// Internally we use a doubly-linked list to track access order and
// a hash map for O(1) lookups. Every get/set moves the entry to the
// front of the list, so the back is always the LRU candidate.

struct CacheEntry {
    std::string value;
    // Optional TTL: if set, the entry expires after this time point
    std::optional<std::chrono::steady_clock::time_point> expires_at;
};

class LRUCache {
public:
    // capacity: max number of keys the cache will hold before evicting
    explicit LRUCache(size_t capacity);

    // GET: returns the value for key, or std::nullopt if missing/expired.
    // Moves the entry to front (marks as recently used).
    std::optional<std::string> get(const std::string& key);

    // SET: inserts or updates key->value. If ttl_ms > 0, the key expires
    // after that many milliseconds. Evicts LRU entry if at capacity.
    void set(const std::string& key, const std::string& value, int ttl_ms = 0);

    // DEL: removes a key. Returns true if the key existed.
    bool del(const std::string& key);

    // EXISTS: checks whether a key is present and not expired.
    bool exists(const std::string& key);

    // Returns the current number of live keys in the cache.
    size_t size();

    // Scan and remove all entries whose TTL has passed.
    // Called periodically by a background thread in the server.
    void evict_expired();

private:
    size_t capacity_;

    // The list stores (key, entry) pairs in MRU-to-LRU order.
    // Front = most recently used, back = least recently used.
    using ListNode = std::pair<std::string, CacheEntry>;
    std::list<ListNode> usage_list_;

    // Maps each key to its iterator in usage_list_ for O(1) access.
    std::unordered_map<std::string, std::list<ListNode>::iterator> map_;

    // Guards all reads and writes. Every public method locks this.
    std::mutex mutex_;

    // Internal helper: remove the node at the back of the list (LRU).
    void evict_lru_locked();

    // Internal helper: check if an entry is expired (no lock needed,
    // caller is responsible for holding mutex_).
    bool is_expired_locked(const CacheEntry& entry) const;
};