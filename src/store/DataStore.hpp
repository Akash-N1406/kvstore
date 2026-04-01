#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <list> // ← NEW

using TimePoint = std::chrono::steady_clock::time_point;

class DataStore
{
public:
    // capacity: maximum number of keys before LRU eviction kicks in.
    // Default 10000 — easy to override in main.cpp later.
    explicit DataStore(size_t capacity = 10000);
    ~DataStore();

    bool set(const std::string &key, const std::string &value);
    std::optional<std::string> get(const std::string &key);
    bool del(const std::string &key);
    bool expire(const std::string &key, int seconds);
    size_t size();

private:
    // ── Storage ───────────────────────────────────────────────────────────
    std::unordered_map<std::string, std::string> m_store;
    std::unordered_map<std::string, TimePoint> m_ttl;

    // ── LRU structures ────────────────────────────────────────────────────
    // The list holds keys in recency order: front = MRU, back = LRU.
    std::list<std::string> m_lruList;

    // Maps each key → its iterator in m_lruList.
    // list iterators are stable — they never invalidate unless the node
    // itself is erased. Safe to store long-term.
    std::unordered_map<std::string,
                       std::list<std::string>::iterator>
        m_lruMap;

    size_t m_capacity; // max keys before eviction

    // ── Concurrency ───────────────────────────────────────────────────────
    mutable std::shared_mutex m_mutex;

    // ── Expiry thread ─────────────────────────────────────────────────────
    std::thread m_expiryThread;
    std::atomic<bool> m_stopExpiry;

    void expiryLoop();
    void purgeExpired();
    bool isExpired(const std::string &key) const;

    // ── LRU helpers ───────────────────────────────────────────────────────
    // Touch: move key to front of list (= mark as most recently used).
    // Must be called with exclusive lock held.
    void lruTouch(const std::string &key);

    // Evict: remove the least recently used key (list back).
    // Must be called with exclusive lock held.
    void lruEvict();
};