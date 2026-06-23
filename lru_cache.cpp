#include "lru_cache.h"
#include <stdexcept>

LRUCache::LRUCache(size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("Cache capacity must be > 0");
    }
}

// ---- private helpers -------------------------------------------------------

bool LRUCache::is_expired_locked(const CacheEntry& entry) const {
    if (!entry.expires_at.has_value()) return false;
    return std::chrono::steady_clock::now() > entry.expires_at.value();
}

void LRUCache::evict_lru_locked() {
    if (usage_list_.empty()) return;

    // The back of the list is the least-recently-used entry.
    auto lru_it = std::prev(usage_list_.end());
    map_.erase(lru_it->first);
    usage_list_.erase(lru_it);
}

// ---- public API ------------------------------------------------------------

std::optional<std::string> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) {
        return std::nullopt; // key doesn't exist
    }

    auto list_it = it->second;

    // Check TTL before returning the value
    if (is_expired_locked(list_it->second)) {
        // Lazy expiry: remove it now rather than waiting for the background sweep
        usage_list_.erase(list_it);
        map_.erase(it);
        return std::nullopt;
    }

    // Move this node to the front of the list (most recently used)
    usage_list_.splice(usage_list_.begin(), usage_list_, list_it);

    return list_it->second.value;
}

void LRUCache::set(const std::string& key, const std::string& value, int ttl_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    CacheEntry entry;
    entry.value = value;

    if (ttl_ms > 0) {
        entry.expires_at = std::chrono::steady_clock::now()
                         + std::chrono::milliseconds(ttl_ms);
    }

    auto it = map_.find(key);
    if (it != map_.end()) {
        // Key already exists: update in place and move to front
        auto list_it = it->second;
        list_it->second = entry;
        usage_list_.splice(usage_list_.begin(), usage_list_, list_it);
        return;
    }

    // New key: check if we need to evict before inserting
    if (usage_list_.size() >= capacity_) {
        evict_lru_locked();
    }

    // Insert at the front (most recently used position)
    usage_list_.emplace_front(key, entry);
    map_[key] = usage_list_.begin();
}

bool LRUCache::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) return false;

    usage_list_.erase(it->second);
    map_.erase(it);
    return true;
}

bool LRUCache::exists(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = map_.find(key);
    if (it == map_.end()) return false;

    if (is_expired_locked(it->second->second)) {
        usage_list_.erase(it->second);
        map_.erase(it);
        return false;
    }

    return true;
}

size_t LRUCache::size() {
    std::lock_guard<std::mutex> lock(mutex_);
    return map_.size();
}

void LRUCache::evict_expired() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Walk the list and collect expired keys.
    // We can't erase while iterating, so we gather them first.
    std::vector<std::string> to_remove;
    for (auto& node : usage_list_) {
        if (is_expired_locked(node.second)) {
            to_remove.push_back(node.first);
        }
    }

    for (const auto& key : to_remove) {
        auto it = map_.find(key);
        if (it != map_.end()) {
            usage_list_.erase(it->second);
            map_.erase(it);
        }
    }
}