#include "server/Server.hpp"
#include "utils/Logger.hpp"

#include <csignal>   // signal(), SIGINT, SIGTERM
#include <cstdlib>   // EXIT_SUCCESS / EXIT_FAILURE
#include <stdexcept> // std::exception

// Global pointer so the signal handler can reach the server instance.
// Raw pointer is fine here — it's purely for signal handling, not ownership.
static Server *g_server = nullptr;

void signalHandler(int signo)
{
    LOG_INFO("\nSignal " << signo << " received — shutting down...");
    if (g_server)
        g_server->stop();
}

int main()
{
    // Default port 6379 matches Redis — muscle memory from redis-cli carries over.
    constexpr int PORT = 6379;
    constexpr int MAX_CONN = 128;

    LOG_INFO("kvstore v0.1 — Step 1: TCP Server");

    try
    {
        Server server(PORT, MAX_CONN);
        g_server = &server;

        // Register clean shutdown on Ctrl+C and kill.
        signal(SIGINT, signalHandler);
        signal(SIGTERM, signalHandler);

        server.run(); // blocks until stop() is called
    }
    catch (const std::exception &e)
    {
        LOG_ERROR("Fatal: " << e.what());
        return EXIT_FAILURE;
    }

    LOG_INFO("kvstore shut down cleanly.");
    return EXIT_SUCCESS;
}