#include "threadpool/ThreadPool.hpp"
#include "utils/Logger.hpp"

ThreadPool::ThreadPool(size_t numThreads) : m_stop(false) {
    LOG_INFO("ThreadPool: starting " << numThreads << " worker threads");
    for (size_t i = 0; i < numThreads; ++i) {
        m_workers.emplace_back(&ThreadPool::workerLoop, this);
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_cv.notify_all();
    for (std::thread& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    LOG_INFO("ThreadPool: all workers stopped");
}

void ThreadPool::enqueue(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop) {
            LOG_WARN("ThreadPool: enqueue called after stop — task dropped");
            return;
        }
        m_tasks.push(std::move(task));
    }
    m_cv.notify_one();
}

void ThreadPool::workerLoop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] {
                return m_stop || !m_tasks.empty();
            });
            if (m_stop && m_tasks.empty()) return;
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task();
    }
}
