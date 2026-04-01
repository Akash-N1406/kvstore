#pragma once

#include <atomic>
#include <string>

class Server
{
public:
    // port      — TCP port to listen on (default 6379, same as Redis)
    // maxConn   — listen() backlog: max pending connections in the OS queue
    Server(int port, int maxConn = 128);
    ~Server();

    // Blocks forever; call stop() from a signal handler to exit cleanly.
    void run();
    void stop();

private:
    int m_port;
    int m_maxConn;
    int m_serverFd; // the listening socket fd
    std::atomic<bool> m_running;

    // Creates, binds, and begins listening on m_serverFd.
    void setupSocket();

    // Called in a new std::thread for every accepted client_fd.
    // (In Step 2 this becomes a thread-pool task.)
    void handleClient(int clientFd);
};