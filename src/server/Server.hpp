#pragma once

#include <atomic>
#include <string>
#include "threadpool/ThreadPool.hpp"
#include "store/DataStore.hpp" // ← ADD

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

    ThreadPool m_pool;
    DataStore m_store{10000}; // ← ADD

    void setupSocket();
    void handleClient(int clientFd);
};