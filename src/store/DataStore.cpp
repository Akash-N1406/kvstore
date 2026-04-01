#include "store/DataStore.hpp"
#include "utils/Logger.hpp"

#include <chrono>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;
using seconds_t = std::chrono::seconds;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor — start the background expiry thread
// ─────────────────────────────────────────────────────────────────────────────

DataStore::DataStore() : m_stopExpiry(false)
{
    // Launch the expiry thread. It runs expiryLoop() until m_stopExpiry == true.
    m_expiryThread = std::thread(&DataStore::expiryLoop, this);
    LOG_INFO("DataStore: initialised, expiry thread started");
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor — stop the expiry thread cleanly before destroying data
// ─────────────────────────────────────────────────────────────────────────────

DataStore::~DataStore()
{
    m_stopExpiry = true;
    // join() waits for expiryLoop() to return — guarantees no use-after-free.
    if (m_expiryThread.joinable())
    {
        m_expiryThread.join();
    }
    LOG_INFO("DataStore: expiry thread stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// isExpired — check if a key has a TTL entry that has passed
// Caller MUST hold at least a shared lock before calling this.
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::isExpired(const std::string &key) const
{
    auto it = m_ttl.find(key);
    if (it == m_ttl.end())
        return false;                         // no TTL → never expires
    return steady_clock::now() >= it->second; // past deadline → expired
}

// ─────────────────────────────────────────────────────────────────────────────
// set — store a key/value pair
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::set(const std::string &key, const std::string &value)
{
    // unique_lock = exclusive write access
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    m_store[key] = value;

    // Setting a key clears its previous TTL (Redis behaviour).
    // If you SET a key that was about to expire, it lives forever again.
    m_ttl.erase(key);

    LOG_DEBUG("SET " << key << " = " << value);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// get — retrieve a value by key
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::string> DataStore::get(const std::string &key)
{
    // shared_lock = concurrent read access — many GETs can run at the same time
    std::shared_lock<std::shared_mutex> lock(m_mutex);

    // Check expiry first — don't return a value for an expired key.
    // The key will be physically removed by the background thread shortly.
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

    LOG_DEBUG("GET " << key << " → " << it->second);
    return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// del — delete a key and its TTL entry
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::del(const std::string &key)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    auto it = m_store.find(key);
    if (it == m_store.end())
    {
        LOG_DEBUG("DEL " << key << " → not found");
        return false;
    }

    m_store.erase(it);
    m_ttl.erase(key); // remove TTL entry if present (erase on missing key is a no-op)

    LOG_DEBUG("DEL " << key << " → deleted");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// expire — set a TTL on an existing key
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::expire(const std::string &key, int seconds)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    // EXPIRE only works on existing, non-expired keys.
    if (m_store.find(key) == m_store.end() || isExpired(key))
    {
        LOG_DEBUG("EXPIRE " << key << " → key not found");
        return false;
    }

    // Calculate the absolute expiry time_point.
    // steady_clock::now() + duration = point in the future.
    m_ttl[key] = steady_clock::now() + seconds_t(seconds);

    LOG_DEBUG("EXPIRE " << key << " in " << seconds << "s");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// size — number of live keys (excludes expired ones)
// ─────────────────────────────────────────────────────────────────────────────

size_t DataStore::size()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_store.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// purgeExpired — called by the expiry thread under exclusive lock
// Walks the entire TTL map and removes any key whose deadline has passed.
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::purgeExpired()
{
    auto now = steady_clock::now();
    int count = 0;

    // Iterate with an explicit iterator so we can erase during the loop.
    // Using a range-for and erasing inside it is undefined behaviour.
    auto it = m_ttl.begin();
    while (it != m_ttl.end())
    {
        if (now >= it->second)
        {
            LOG_DEBUG("Expiry: removing key '" << it->first << "'");
            m_store.erase(it->first); // remove from primary store
            it = m_ttl.erase(it);     // remove from TTL map, advance iterator
            ++count;
        }
        else
        {
            ++it;
        }
    }

    if (count > 0)
    {
        LOG_INFO("DataStore: purged " << count << " expired key(s)");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// expiryLoop — runs on the background thread for the store's lifetime
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::expiryLoop()
{
    while (!m_stopExpiry)
    {
        // Sleep 100ms between scans. This is the expiry resolution:
        // a key set to expire in 1s will be removed within 1.1s at worst.
        // Lower = more precise but more CPU; 100ms is a good balance.
        std::this_thread::sleep_for(100ms);

        // Acquire exclusive lock — purgeExpired modifies both maps.
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        purgeExpired();
    }
}