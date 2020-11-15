#include "listener.hpp"
#include "http_session.hpp"

listener::listener(
    net::io_context& ioc,
    tcp::endpoint endpoint,
    std::shared_ptr<shared_state> const& state)
    : ioc_(ioc)
    , acceptor_(ioc)
    , state_(state)
{
    beast::error_code ec;

    // Open the acceptor
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
    {
        fail(ec, "open");
        return;
    }

    // Allow address reuse
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
    {
        fail(ec, "set_option");
        return;
    }

    // Bind to the server address
    acceptor_.bind(endpoint, ec);
    if (ec)
    {
        fail(ec, "bind");
        return;
    }

    // Start listening for connections
    acceptor_.listen(
        net::socket_base::max_listen_connections, ec);
    if (ec)
    {
        fail(ec, "listen");
        return;
    }
}

// Start accepting incoming connections
void listener::run()
{
    do_accept();
}

void listener::do_accept()
{
    // The new connection gets its own strand
    acceptor_.async_accept(
        net::make_strand(ioc_),
        beast::bind_front_handler(
            &listener::on_accept,
            shared_from_this()));
}

void listener::on_accept(beast::error_code ec, tcp::socket socket)
{
    if (ec)
        fail(ec, "accept");
    else
    {
        // Create the session and run it
        std::make_shared<http_session>(
            std::move(socket),
            state_)->run();
    }

    // Accept another connection
    do_accept();
}
