#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <shared_mutex>
#include <chrono>
#include <thread>
#include <atomic>
#include <list>

using TimePoint = std::chrono::steady_clock::time_point;

class DataStore
{
public:
    // capacity        — max keys before LRU eviction
    // snapshotPath    — file to persist to (e.g. "kvstore.snap")
    // snapshotInterval— seconds between snapshots (0 = disabled)
    explicit DataStore(size_t capacity = 10000,
                       std::string snapshotPath = "kvstore.snap",
                       int snapshotInterval = 300);
    ~DataStore();

    bool set(const std::string &key, const std::string &value);
    std::optional<std::string> get(const std::string &key);
    bool del(const std::string &key);
    bool expire(const std::string &key, int seconds);
    size_t size();

    // Called once at startup — loads the snapshot file if it exists.
    void loadSnapshot();

    // Called on clean shutdown — saves a final snapshot immediately.
    void saveSnapshot();

private:
    // ── Storage ───────────────────────────────────────────────────────────
    std::unordered_map<std::string, std::string> m_store;
    std::unordered_map<std::string, TimePoint> m_ttl;

    // ── LRU ───────────────────────────────────────────────────────────────
    std::list<std::string> m_lruList;
    std::unordered_map<std::string,
                       std::list<std::string>::iterator>
        m_lruMap;
    size_t m_capacity;

    // ── Concurrency ───────────────────────────────────────────────────────
    mutable std::shared_mutex m_mutex;

    // ── Expiry thread ─────────────────────────────────────────────────────
    std::thread m_expiryThread;
    std::atomic<bool> m_stopExpiry;

    // ── Snapshot thread ───────────────────────────────────────────────────
    std::thread m_snapshotThread;
    std::atomic<bool> m_stopSnapshot;
    std::string m_snapshotPath;
    int m_snapshotInterval; // seconds

    // ── Private helpers ───────────────────────────────────────────────────
    void expiryLoop();
    void purgeExpired();
    bool isExpired(const std::string &key) const;

    void lruTouch(const std::string &key);
    void lruEvict();

    void snapshotLoop();

    // Internal save/load — callers must manage locking themselves.
    // saveSnapshot() acquires shared lock internally.
    // loadSnapshot() runs before threads start — no lock needed.
    void saveSnapshotInternal();
};