#include "beast.hpp"
#include "listener.hpp"
#include "shared_state.hpp"
#include <boost/asio/signal_set.hpp>
#include <memory>
#include <vector>
#include <filesystem>

#define THREADS 4

int main(int argc, char* argv[])
{
    // Arguments
    auto const address = net::ip::make_address("0.0.0.0");
    auto const port = static_cast<unsigned short>(8080);
    auto const doc_root = std::filesystem::absolute("../../client").string();

    // The io_context is required for all I/O
    net::io_context ioc{ THREADS };

    // Create and launch a listening port
    std::make_shared<listener>(
        ioc,
        tcp::endpoint{ address, port },
        std::make_shared<shared_state>(doc_root))->run();

    // Capture SIGINT and SIGTERM to perform a clean shutdown
    net::signal_set signals(ioc, SIGINT, SIGTERM);
    signals.async_wait(
        [&ioc](boost::system::error_code const&, int)
        {
            // Stop the io_context. This will cause run()
            // to return immediately, eventually destroying the
            // io_context and any remaining handlers in it.
            ioc.stop();
        });

    // Run the I/O service on the requested number of threads
    std::vector<std::thread> v;
    v.reserve(THREADS - 1);
    for (auto i = THREADS - 1; i > 0; --i)
        v.emplace_back(
            [&ioc]
            {
                ioc.run();
            });
    ioc.run();

    for (auto& t : v)
        t.join();

    return EXIT_SUCCESS;
}