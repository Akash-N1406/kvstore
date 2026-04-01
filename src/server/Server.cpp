#include "server/Server.hpp"
#include "utils/Logger.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#include <cstring>
#include <stdexcept>
#include <thread>
#include <cerrno>

Server::Server(int port, int maxConn)
    : m_port(port), m_maxConn(maxConn), m_serverFd(-1), m_running(false)
{}

Server::~Server() {
    stop();
}

void Server::setupSocket() {
    m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0)
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

    int opt = 1;
    if (setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("setsockopt() failed: " + std::string(strerror(errno)));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(m_port);

    if (bind(m_serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

    if (listen(m_serverFd, m_maxConn) < 0)
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));

    LOG_INFO("Listening on port " << m_port);
}

void Server::run() {
    setupSocket();
    m_running = true;
    LOG_INFO("Server started. Waiting for connections...");

    while (m_running) {
        sockaddr_in clientAddr{};
        socklen_t   clientLen = sizeof(clientAddr);

        int clientFd = accept(m_serverFd,
                              reinterpret_cast<sockaddr*>(&clientAddr),
                              &clientLen);

        if (clientFd < 0) {
            if (!m_running) break;
            LOG_WARN("accept() failed: " << strerror(errno));
            continue;
        }

        std::string clientIp = inet_ntoa(clientAddr.sin_addr);
        LOG_INFO("New connection from " << clientIp << " (fd=" << clientFd << ")");

        std::thread([this, clientFd]() {
            handleClient(clientFd);
        }).detach();
    }

    LOG_INFO("Accept loop exited.");
}

void Server::handleClient(int clientFd) {
    char    buf[4096];
    ssize_t bytesRead;

    const char* banner = "+OK kvstore ready\r\n";
    write(clientFd, banner, strlen(banner));

    while ((bytesRead = read(clientFd, buf, sizeof(buf) - 1)) > 0) {
        buf[bytesRead] = '\0';

        std::string input(buf);
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();

        if (input.empty()) continue;

        LOG_DEBUG("fd=" << clientFd << " recv: " << input);

        std::string response = "+ECHO " + input + "\r\n";
        write(clientFd, response.c_str(), response.size());
    }

    if (bytesRead == 0)
        LOG_INFO("Client fd=" << clientFd << " disconnected gracefully.")
    
    else
        LOG_WARN("read() error on fd=" << clientFd << ": " << strerror(errno))
    
    close(clientFd);
}

void Server::stop() {
    m_running = false;
    if (m_serverFd >= 0) {
        close(m_serverFd);
        m_serverFd = -1;
    }
}
