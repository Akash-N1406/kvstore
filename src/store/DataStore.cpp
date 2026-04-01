#include "store/DataStore.hpp"
#include "utils/Logger.hpp"

#include <chrono>
#include <thread>

using namespace std::chrono;
using namespace std::chrono_literals;
using seconds_t = std::chrono::seconds;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

DataStore::DataStore(size_t capacity)
    : m_capacity(capacity), m_stopExpiry(false)
{
    m_expiryThread = std::thread(&DataStore::expiryLoop, this);
    LOG_INFO("DataStore: initialised, capacity=" << capacity
                                                 << ", expiry thread started");
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor
// ─────────────────────────────────────────────────────────────────────────────

DataStore::~DataStore()
{
    m_stopExpiry = true;
    if (m_expiryThread.joinable())
        m_expiryThread.join();
    LOG_INFO("DataStore: expiry thread stopped");
}

// ─────────────────────────────────────────────────────────────────────────────
// isExpired — caller must hold at least shared lock
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::isExpired(const std::string &key) const
{
    auto it = m_ttl.find(key);
    if (it == m_ttl.end())
        return false;
    return steady_clock::now() >= it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// lruTouch — move key to front of list (MRU position)
// Caller must hold exclusive lock.
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::lruTouch(const std::string &key)
{
    auto it = m_lruMap.find(key);
    if (it == m_lruMap.end())
    {
        // Key not in LRU list yet — insert at front.
        m_lruList.push_front(key);
        m_lruMap[key] = m_lruList.begin();
    }
    else
    {
        // Key already in list — splice its node to the front.
        // splice(destination, source_list, source_iterator)
        // Moves the single node pointed to by it->second to position begin().
        // O(1): just pointer relinking, no memory allocation or copying.
        m_lruList.splice(m_lruList.begin(), m_lruList, it->second);
        // The iterator in m_lruMap still points to the same node —
        // splice doesn't invalidate list iterators. No map update needed.
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// lruEvict — remove the LRU key (back of list) from all structures
// Caller must hold exclusive lock.
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::lruEvict()
{
    if (m_lruList.empty())
        return;

    // The key to evict is at the back — least recently used.
    const std::string &victim = m_lruList.back();

    LOG_INFO("DataStore: LRU evicting key '" << victim << "'");

    // Remove from all three structures.
    m_store.erase(victim);
    m_ttl.erase(victim); // no-op if no TTL
    m_lruMap.erase(victim);
    m_lruList.pop_back();
}

// ─────────────────────────────────────────────────────────────────────────────
// set
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::set(const std::string &key, const std::string &value)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    bool isNew = (m_store.find(key) == m_store.end());

    m_store[key] = value;
    m_ttl.erase(key); // SET clears TTL (Redis behaviour)
    lruTouch(key);    // mark as MRU

    // Only evict if this was a genuinely new key that pushed us over capacity.
    // Overwriting an existing key doesn't increase the key count.
    if (isNew && m_store.size() > m_capacity)
    {
        lruEvict();
    }

    LOG_DEBUG("SET " << key << " = " << value);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// get
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::string> DataStore::get(const std::string &key)
{
    // GET needs to touch the LRU list (write operation) even though
    // it's a read on the value. So we need an exclusive lock here.
    // In a more advanced design you'd separate the LRU lock from the
    // data lock — for now exclusive is correct and safe.
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

    // Key exists and is live — promote it to MRU.
    lruTouch(key);

    LOG_DEBUG("GET " << key << " → " << it->second);
    return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// del
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

    // Remove from all structures.
    m_store.erase(it);
    m_ttl.erase(key);

    // Remove from LRU list and map.
    auto lruIt = m_lruMap.find(key);
    if (lruIt != m_lruMap.end())
    {
        m_lruList.erase(lruIt->second); // O(1) — we have the iterator
        m_lruMap.erase(lruIt);
    }

    LOG_DEBUG("DEL " << key << " → deleted");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// expire
// ─────────────────────────────────────────────────────────────────────────────

bool DataStore::expire(const std::string &key, int seconds)
{
    std::unique_lock<std::shared_mutex> lock(m_mutex);

    if (m_store.find(key) == m_store.end() || isExpired(key))
    {
        LOG_DEBUG("EXPIRE " << key << " → key not found");
        return false;
    }

    m_ttl[key] = steady_clock::now() + seconds_t(seconds);
    LOG_DEBUG("EXPIRE " << key << " in " << seconds << "s");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// size
// ─────────────────────────────────────────────────────────────────────────────

size_t DataStore::size()
{
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    return m_store.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// purgeExpired — called by expiry thread under exclusive lock
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
            LOG_DEBUG("Expiry: removing key '" << key << "'");

            // Remove from LRU structures too — expired key must leave everywhere.
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

// ─────────────────────────────────────────────────────────────────────────────
// expiryLoop
// ─────────────────────────────────────────────────────────────────────────────

void DataStore::expiryLoop()
{
    while (!m_stopExpiry)
    {
        std::this_thread::sleep_for(100ms);
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        purgeExpired();
    }
}