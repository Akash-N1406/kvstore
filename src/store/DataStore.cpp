#include "store/DataStore.hpp"
#include "utils/Logger.hpp"

#include <chrono>
#include <thread>
#include <fstream>
#include <cstring>

using namespace std::chrono;
using namespace std::chrono_literals;
using seconds_t = std::chrono::seconds;

// ─────────────────────────────────────────────────────────────────────────────
// Serialisation helpers
//
// We write raw binary — no JSON, no text parsing overhead.
// Format per record:
//   uint32_t  key_len
//   char[]    key  (key_len bytes)
//   uint32_t  val_len
//   char[]    val  (val_len bytes)
//   int64_t   ttl_ms  (-1 = no TTL, else ms since Unix epoch)
// ─────────────────────────────────────────────────────────────────────────────

// Write a length-prefixed string to a binary stream.
static void writeString(std::ofstream &out, const std::string &s)
{
    uint32_t len = static_cast<uint32_t>(s.size());
    out.write(reinterpret_cast<const char *>(&len), sizeof(len));
    out.write(s.data(), len);
}

// Read a length-prefixed string from a binary stream.
static std::string readString(std::ifstream &in)
{
    uint32_t len = 0;
    in.read(reinterpret_cast<char *>(&len), sizeof(len));
    std::string s(len, '\0');
    in.read(s.data(), len);
    return s;
}

// Convert a steady_clock TimePoint → milliseconds since Unix epoch (int64_t).
// We use system_clock for the absolute timestamp because steady_clock
// has no defined epoch — it can't be stored and restored across restarts.
static int64_t toEpochMs(const TimePoint &tp)
{
    // Duration from steady_clock epoch to tp
    auto steadyDur = tp - steady_clock::now();
    // Convert to system_clock equivalent
    auto sysTp = system_clock::now() + duration_cast<system_clock::duration>(steadyDur);
    return duration_cast<milliseconds>(sysTp.time_since_epoch()).count();
}

// Convert milliseconds since Unix epoch → steady_clock TimePoint.
static TimePoint fromEpochMs(int64_t ms)
{
    auto sysDur = milliseconds(ms);
    auto sysTp = system_clock::time_point(sysDur);
    auto steadyDur = sysTp - system_clock::now();
    return steady_clock::now() + duration_cast<steady_clock::duration>(steadyDur);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DataStore::DataStore(size_t capacity, std::string snapshotPath, int snapshotInterval)
    : m_capacity(capacity),
      m_stopExpiry(false),
      m_stopSnapshot(false),
      m_snapshotPath(std::move(snapshotPath)),
      m_snapshotInterval(snapshotInterval)
{
    m_expiryThread = std::thread(&DataStore::expiryLoop, this);
    m_snapshotThread = std::thread(&DataStore::snapshotLoop, this);

    LOG_INFO("DataStore: capacity=" << capacity
                                    << " snapshotPath=" << m_snapshotPath
                                    << " snapshotInterval=" << snapshotInterval << "s");
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor — save a final snapshot before dying
// ─────────────────────────────────────────────────────────────────────────────

DataStore::~DataStore()
{
    // Stop background threads first.
    m_stopExpiry = true;
    m_stopSnapshot = true;

    if (m_expiryThread.joinable())
        m_expiryThread.join();
    if (m_snapshotThread.joinable())
        m_snapshotThread.join();

    // Save one final snapshot on clean shutdown.
    LOG_INFO("DataStore: saving final snapshot...");
    saveSnapshot();

    LOG_INFO("DataStore: shut down complete");
}

// ─────────────────────────────────────────────────────────────────────────────
// isExpired
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::isExpired(const std::string &key) const
{
    auto it = m_ttl.find(key);
    if (it == m_ttl.end())
        return false;
    return steady_clock::now() >= it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// LRU helpers
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::lruTouch(const std::string &key)
{
    auto it = m_lruMap.find(key);
    if (it == m_lruMap.end())
    {
        m_lruList.push_front(key);
        m_lruMap[key] = m_lruList.begin();
    }
    else
    {
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
    }
}

void DataStore::lruEvict()
{
    if (m_lruList.empty())
        return;
    const std::string &victim = m_lruList.back();
    LOG_INFO("DataStore: LRU evicting key '" << victim << "'");
    m_store.erase(victim);
    m_ttl.erase(victim);
    m_lruMap.erase(victim);
    m_lruList.pop_back();
}

// ─────────────────────────────────────────────────────────────────────────────
// set / get / del / expire / size
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::set(const std::string &key, const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    bool isNew = (m_store.find(key) == m_store.end());
    m_store[key] = value;
    m_ttl.erase(key);
    lruTouch(key);
    if (isNew && m_store.size() > m_capacity)
        lruEvict();
    LOG_DEBUG("SET " << key << " = " << value);
    return true;
}

std::optional<std::string> DataStore::get(const std::string &key)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (isExpired(key))
    {
        LOG_DEBUG("GET " << key << " → expired");
        return std::nullopt;
    }
    auto it = m_store.find(key);
    if (it == m_store.end())
    {
        LOG_DEBUG("GET " << key << " → not found");
        return std::nullopt;
    }
    lruTouch(key);
    LOG_DEBUG("GET " << key << " → " << it->second);
    return it->second;
}

bool DataStore::del(const std::string &key)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_store.find(key);
    if (it == m_store.end())
        return false;
    m_store.erase(it);
    m_ttl.erase(key);
    auto lruIt = m_lruMap.find(key);
    if (lruIt != m_lruMap.end())
    {
        m_lruList.erase(lruIt->second);
        m_lruMap.erase(lruIt);
    }
    LOG_DEBUG("DEL " << key << " → deleted");
    return true;
}

bool DataStore::expire(const std::string &key, int seconds)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);
    if (m_store.find(key) == m_store.end() || isExpired(key))
        return false;
    m_ttl[key] = steady_clock::now() + seconds_t(seconds);
    LOG_DEBUG("EXPIRE " << key << " in " << seconds << "s");
    return true;
}

size_t DataStore::size()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_store.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// saveSnapshotInternal — must be called with NO lock held (acquires shared)
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::saveSnapshotInternal()
{
    // Write to a temp file first — rename is atomic on Linux.
    std::string tmpPath = m_snapshotPath + ".tmp";
    std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);

    if (!out.is_open())
    {
        LOG_ERROR("DataStore: cannot open " << tmpPath << " for writing");
        return;
    }

    // Acquire shared lock — readers can still run during snapshot.
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    auto now = steady_clock::now();

    // Count how many keys are non-expired (skip expired ones from snapshot).
    uint64_t count = 0;
    for (auto &[key, _] : m_store)
    {
        auto ttlIt = m_ttl.find(key);
        if (ttlIt != m_ttl.end() && now >= ttlIt->second)
            continue;
        ++count;
    }

    // Write header: number of entries.
    out.write(reinterpret_cast<const char *>(&count), sizeof(count));

    // Write each non-expired entry.
    for (auto &[key, value] : m_store)
    {
        auto ttlIt = m_ttl.find(key);
        bool expired = (ttlIt != m_ttl.end() && now >= ttlIt->second);
        if (expired)
            continue;

        writeString(out, key);
        writeString(out, value);

        // TTL: store absolute epoch ms, or -1 if no TTL.
        int64_t ttlMs = -1;
        if (ttlIt != m_ttl.end())
        {
            ttlMs = toEpochMs(ttlIt->second);
        }
        out.write(reinterpret_cast<const char *>(&ttlMs), sizeof(ttlMs));
    }

    out.close();
    lock.unlock();

    // Atomic replace: rename .tmp → real file.
    if (std::rename(tmpPath.c_str(), m_snapshotPath.c_str()) != 0)
    {
        LOG_ERROR("DataStore: rename failed: " << strerror(errno));
        return;
    }

    LOG_INFO("DataStore: snapshot saved (" << count << " keys) → " << m_snapshotPath);
}

void DataStore::saveSnapshot()
{
    saveSnapshotInternal();
}

// ─────────────────────────────────────────────────────────────────────────────
// loadSnapshot — call once at startup, before threads begin
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::loadSnapshot()
{
    std::ifstream in(m_snapshotPath, std::ios::binary);
    if (!in.is_open())
    {
        LOG_INFO("DataStore: no snapshot found at " << m_snapshotPath << " — starting fresh");
        return;
    }

    uint64_t count = 0;
    in.read(reinterpret_cast<char *>(&count), sizeof(count));

    if (!in)
    {
        LOG_ERROR("DataStore: snapshot file corrupt — starting fresh");
        return;
    }

    auto now = system_clock::now();
    int loaded = 0;
    int skipped = 0;

    for (uint64_t i = 0; i < count; ++i)
    {
        std::string key = readString(in);
        std::string value = readString(in);

        int64_t ttlMs = -1;
        in.read(reinterpret_cast<char *>(&ttlMs), sizeof(ttlMs));

        if (!in)
        {
            LOG_ERROR("DataStore: snapshot truncated at entry " << i);
            break;
        }

        // Skip keys that already expired while server was down.
        if (ttlMs != -1)
        {
            auto expiry = system_clock::time_point(milliseconds(ttlMs));
            if (now >= expiry)
            {
                ++skipped;
                continue;
            }
        }

        // Insert directly — bypass the public set() to avoid lock overhead
        // (we're single-threaded at load time).
        m_store[key] = value;
        lruTouch(key);

        if (ttlMs != -1)
        {
            m_ttl[key] = fromEpochMs(ttlMs);
        }

        ++loaded;
    }

    LOG_INFO("DataStore: loaded " << loaded << " keys, skipped "
                                  << skipped << " expired keys from " << m_snapshotPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// snapshotLoop — background thread
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::snapshotLoop()
{
    if (m_snapshotInterval <= 0)
    {
        LOG_INFO("DataStore: snapshotting disabled");
        return;
    }

    while (!m_stopSnapshot)
    {
        // Sleep in 1-second increments so we respond to m_stopSnapshot quickly.
        for (int i = 0; i < m_snapshotInterval && !m_stopSnapshot; ++i)
        {
            std::this_thread::sleep_for(1s);
        }
        if (!m_stopSnapshot)
        {
            saveSnapshotInternal();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// purgeExpired + expiryLoop (unchanged from Step 5)
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::purgeExpired()
{
    auto now = steady_clock::now();
    int count = 0;
    auto it = m_ttl.begin();

    while (it != m_ttl.end())
    {
        if (now >= it->second)
        {
            const std::string &key = it->first;
            auto lruIt = m_lruMap.find(key);
            if (lruIt != m_lruMap.end())
            {
                m_lruList.erase(lruIt->second);
                m_lruMap.erase(lruIt);
            }
            m_store.erase(key);
            it = m_ttl.erase(it);
            ++count;
        }
        else
        {
            ++it;
        }
    }

    if (count > 0)
        LOG_INFO("DataStore: purged " << count << " expired key(s)");
}

void DataStore::expiryLoop()
{
    while (!m_stopExpiry)
    {
        std::this_thread::sleep_for(100ms);
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        purgeExpired();
    }
}