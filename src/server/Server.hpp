#pragma once

#include <atomic>
#include <string>
#include "threadpool/ThreadPool.hpp" // ← ADD THIS

class Server
{
public:
    Server(int port, int maxConn = 128);
    ~Server();

    void run();
    void stop();

private:
    int m_port;
    int m_maxConn;
    int m_serverFd;
    std::atomic<bool> m_running;

    ThreadPool m_pool; // ← ADD THIS

    void setupSocket();
    void handleClient(int clientFd);
};