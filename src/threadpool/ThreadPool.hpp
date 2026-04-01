#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>

class ThreadPool
{
public:
    // numThreads: how many worker threads to create at startup.
    // Rule of thumb: std::thread::hardware_concurrency() for CPU-bound work.
    // For I/O-bound work (like us) you can go higher — 2x or 4x core count.
    explicit ThreadPool(size_t numThreads);

    // Destructor: signals all workers to finish and joins them cleanly.
    ~ThreadPool();

    // enqueue: push any callable (lambda, function, functor) onto the task queue.
    // Returns immediately — the caller never blocks.
    void enqueue(std::function<void()> task);

    // Non-copyable, non-movable — threads hold a pointer to this object.
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    // The actual OS threads — created once, live for the pool's lifetime.
    std::vector<std::thread> m_workers;

    // The shared task queue.
    std::queue<std::function<void()>> m_tasks;

    // Protects m_tasks — only one thread (worker or main) touches it at a time.
    std::mutex m_mutex;

    // Workers sleep on this when the queue is empty.
    // enqueue() calls notify_one() to wake exactly one worker.
    std::condition_variable m_cv;

    // Flipped to true in the destructor. Workers check this to know when to exit.
    std::atomic<bool> m_stop;

    // The function each worker thread runs — an infinite loop of wait → pop → run.
    void workerLoop();
};