#pragma once

#include "beast.hpp"
#include "shared_state.hpp"
#include <boost/beast/version.hpp>

// Handles an HTTP server connection
class http_session : public std::enable_shared_from_this<http_session>
{
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    std::shared_ptr<shared_state> state_;
    // The parser is stored in an optional container so we can
    // construct it from scratch it at the beginning of each new message.
    boost::optional<http::request_parser<http::string_body>> parser_;

public:
    http_session(
        tcp::socket&& socket,
        std::shared_ptr<shared_state> const& state);
    ~http_session() {}
    
    void run();
    
private:
    void do_read();
    void on_read(beast::error_code ec, std::size_t);
    void send_payload(std::string const& payload);
    template <class BodyType>
    void write(http::response<BodyType>&& res);
    void on_write(beast::error_code ec, std::size_t, bool close);
    void do_close();

    template<class Body, class Allocator>
    void handle_request(http::request<Body, http::basic_fields<Allocator>>&& req);
};