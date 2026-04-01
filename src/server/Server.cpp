#include "server/Server.hpp"
#include "utils/Logger.hpp"
#include "parser/Parser.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <cerrno>

Server::Server(int port, int maxConn)
    : m_port(port),
      m_maxConn(maxConn),
      m_serverFd(-1),
      m_running(false),
      m_pool(std::thread::hardware_concurrency())
{
}

Server::~Server() { stop(); }

void Server::setupSocket()
{
    m_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (m_serverFd < 0)
        throw std::runtime_error("socket() failed: " + std::string(strerror(errno)));

    int opt = 1;
    if (setsockopt(m_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
        throw std::runtime_error("setsockopt() failed: " + std::string(strerror(errno)));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(m_port);

    if (bind(m_serverFd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
        throw std::runtime_error("bind() failed: " + std::string(strerror(errno)));

    if (listen(m_serverFd, m_maxConn) < 0)
        throw std::runtime_error("listen() failed: " + std::string(strerror(errno)));

    LOG_INFO("Listening on port " << m_port);
}

void Server::run()
{
    setupSocket();
    m_running = true;
    LOG_INFO("Server started. Waiting for connections...");

    while (m_running)
    {
        sockaddr_in clientAddr{};
        socklen_t clientLen = sizeof(clientAddr);

        int clientFd = accept(m_serverFd,
                              reinterpret_cast<sockaddr *>(&clientAddr),
                              &clientLen);

        if (clientFd < 0)
        {
            if (!m_running)
                break;
            LOG_WARN("accept() failed: " << strerror(errno));
            continue;
        }

        std::string clientIp = inet_ntoa(clientAddr.sin_addr);
        LOG_INFO("New connection from " << clientIp << " (fd=" << clientFd << ")");

        m_pool.enqueue([this, clientFd]()
                       { handleClient(clientFd); });
    }

    LOG_INFO("Accept loop exited.");
}

void Server::handleClient(int clientFd)
{
    char buf[4096];
    ssize_t bytesRead;

    const char *banner = "+OK kvstore ready\r\n";
    write(clientFd, banner, strlen(banner));

    while ((bytesRead = read(clientFd, buf, sizeof(buf) - 1)) > 0)
    {
        buf[bytesRead] = '\0';

        std::string input(buf);
        while (!input.empty() && (input.back() == '\n' || input.back() == '\r'))
            input.pop_back();

        if (input.empty())
            continue;

        LOG_DEBUG("fd=" << clientFd << " recv: " << input);

        Command cmd = parse(input);
        std::string response;

        if (cmd.type == CommandType::UNKNOWN)
        {
            response = cmd.errorMsg;
        }
        else if (cmd.type == CommandType::PING)
        {
            response = "+PONG\r\n";
        }
        else if (cmd.type == CommandType::SET)
        {
            m_store.set(cmd.args[0], cmd.args[1]);
            response = "+OK\r\n";
        }
        else if (cmd.type == CommandType::GET)
        {
            auto val = m_store.get(cmd.args[0]);
            if (val.has_value())
            {
                // Bulk string response: $<length>\r\n<value>\r\n
                // This is proper Redis protocol — redis-cli expects this format.
                response = "$" + std::to_string(val->size()) + "\r\n";
                response += *val + "\r\n";
            }
            else
            {
                response = "$-1\r\n"; // nil bulk string — key not found
            }
        }
        else if (cmd.type == CommandType::DEL)
        {
            bool deleted = m_store.del(cmd.args[0]);
            // :1 = deleted, :0 = key didn't exist
            response = deleted ? ":1\r\n" : ":0\r\n";
        }
        else if (cmd.type == CommandType::EXPIRE)
        {
            int secs = std::stoi(cmd.args[1]);
            bool ok = m_store.expire(cmd.args[0], secs);
            response = ok ? ":1\r\n" : ":0\r\n";
        }

        write(clientFd, response.c_str(), response.size());
    }

    if (bytesRead == 0)
        LOG_INFO("Client fd=" << clientFd << " disconnected gracefully.")
    else
        LOG_WARN("read() error on fd=" << clientFd << ": " << strerror(errno))

    close(clientFd);
}

void Server::stop()
{
    m_running = false;
    if (m_serverFd >= 0)
    {
        close(m_serverFd);
        m_serverFd = -1;
    }
}