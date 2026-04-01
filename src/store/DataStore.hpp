#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>

// Convenience alias — a point in time from the steady clock.
// steady_clock is monotonic (never goes backward), unlike system_clock
// which can jump when the user changes the system time.
using TimePoint = std::chrono::steady_clock::time_point;

class DataStore
{
public:
    DataStore();
    ~DataStore();

    // SET key value — always succeeds, returns true.
    bool set(const std::string &key, const std::string &value);

    // GET key — returns the value if key exists and hasn't expired.
    // Returns std::nullopt if the key is missing or expired.
    std::optional<std::string> get(const std::string &key);

    // DEL key — removes the key (and its TTL entry if any).
    // Returns true if the key existed, false if it wasn't found.
    bool del(const std::string &key);

    // EXPIRE key seconds — sets a TTL on an existing key.
    // Returns true if the key exists (and TTL was set), false otherwise.
    bool expire(const std::string &key, int seconds);

    // Returns the number of keys currently in the store (for diagnostics).
    size_t size();

private:
    // Primary storage: key → value
    std::unordered_map<std::string, std::string> m_store;

    // TTL storage: key → expiry time_point
    // Only keys that have been given a TTL appear here.
    std::unordered_map<std::string, TimePoint> m_ttl;

    // shared_mutex: allows concurrent reads, exclusive writes.
    // shared_lock  → multiple readers can hold it simultaneously.
    // unique_lock  → exclusive, blocks all readers and writers.
    mutable std::shared_mutex m_mutex;

    // Background expiry thread state
    std::thread m_expiryThread;
    std::atomic<bool> m_stopExpiry;

    // The function the expiry thread runs.
    void expiryLoop();

    // Called inside expiryLoop — checks all TTL entries and removes expired keys.
    // Must be called with exclusive lock held.
    void purgeExpired();

    // Check if a key is expired RIGHT NOW (no lock — caller must hold at least shared).
    bool isExpired(const std::string &key) const;
};