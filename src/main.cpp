#include "server/Server.hpp"
#include "utils/Logger.hpp"

#include <csignal>
#include <cstdlib>
#include <stdexcept>

static Server *g_server = nullptr;

void signalHandler(int signo)
{
    LOG_INFO("\nSignal " << signo << " received — shutting down...");
    if (g_server)
        g_server->stop();
}

int main()
{
    constexpr int PORT = 6379;
    constexpr int MAX_CONN = 128;

    LOG_INFO("kvstore v1.0 — High-Performance In-Memory Key-Value Store");

    try
    {
        Server server(PORT, MAX_CONN);
        g_server = &server;

        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        server.run();
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Fatal: " << e.what());
        return EXIT_FAILURE;
    }

    LOG_INFO("kvstore shut down cleanly.");
    return EXIT_SUCCESS;
}