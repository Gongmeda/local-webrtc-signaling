#include "http_session.hpp"
#include "webrtc_session.hpp"
#include <thread>

// Rapidjson - JSON Parser library
#include <rapidjson/document.h>

http_session::http_session(
    tcp::socket&& socket,
    std::shared_ptr<shared_state> const& state)
	: stream_(std::move(socket))
    , state_(state)
{
}

template<class Body, class Allocator>
void http_session::handle_request(http::request<Body, http::basic_fields<Allocator>>&& req)
{
    if (req.method() == http::verb::post)
    {
        if (req.target() == "/offer")
        {
            // Parse JSON payload for sdp offer message
            rapidjson::Document message;
            message.Parse(req.body().c_str());
            std::string offer_payload_ = message["sdp"].GetString();
            offer_payload_.pop_back();

            // Create WebRTC session in shared_state
            state_->create_session(state_);

            // Create Peer Connection in shared_state
            state_->create_connection(offer_payload_);

            // Wait until corresponding connection's answer is created
            while (true)
                if (state_->is_answer_ready())
                {
                    state_->set_answer_ready_state(false);
                    break;
                }

            // Send answer JSON payload to remote peer
            return send_payload(state_->get_answer_payload());
        }
    }
    // Handle other HTTP requests
    else
    {
        // Returns a bad request response
        auto const bad_request =
            [&req](beast::string_view why)
        {
            http::response<http::string_body> res{ http::status::bad_request, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = std::string(why);
            res.prepare_payload();
            return res;
        };

        // Returns a not found response
        auto const not_found =
            [&req](beast::string_view target)
        {
            http::response<http::string_body> res{ http::status::not_found, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "The resource '" + std::string(target) + "' was not found.";
            res.prepare_payload();
            return res;
        };

        // Returns a server error response
        auto const server_error =
            [&req](beast::string_view what)
        {
            http::response<http::string_body> res{ http::status::internal_server_error, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.body() = "An error occurred: '" + std::string(what) + "'";
            res.prepare_payload();
            return res;
        };

        //Returns a redirect response
        auto const redirect =
            [&req](beast::string_view where)
        {
            http::response<http::string_body> res{ http::status::moved_permanently, req.version() };
            res.set(http::field::location, where);
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, "text/html");
            res.keep_alive(req.keep_alive());
            res.prepare_payload();
            return res;
        };

        // Make sure we can handle the method
        if (req.method() != http::verb::get &&
            req.method() != http::verb::head)
            return write(bad_request("Unknown HTTP-method"));

        // Redirect to index.html
        if (req.target().empty() ||
            req.target().back() == '/' ||
            req.target()[0] != '/')
            return write(redirect("/index.html"));

        // Build the path to the requested file
        std::string path = path_cat(state_->doc_root(), req.target());

        // Attempt to open the file
        beast::error_code ec;
        http::file_body::value_type body;
        body.open(path.c_str(), beast::file_mode::scan, ec);

        // Handle the case where the file doesn't exist
        if (ec == beast::errc::no_such_file_or_directory)
            return write(not_found(req.target()));

        // Handle an unknown error
        if (ec)
            return write(server_error(ec.message()));

        // Cache the size since we need it after the move
        auto const size = body.size();

        // Respond to HEAD request
        if (req.method() == http::verb::head)
        {
            http::response<http::empty_body> res{ http::status::ok, req.version() };
            res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
            res.set(http::field::content_type, mime_type(path));
            res.content_length(size);
            res.keep_alive(req.keep_alive());
            return write(std::move(res));
        }

        // Respond to GET request
        http::response<http::file_body> res{
            std::piecewise_construct,
            std::make_tuple(std::move(body)),
            std::make_tuple(http::status::ok, req.version()) };
        res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
        res.set(http::field::content_type, mime_type(path));
        res.content_length(size);
        res.keep_alive(req.keep_alive());
        return write(std::move(res));
    }
}

void http_session::run()
{
	do_read();
}

void http_session::do_read()
{
    // Construct a new parser for each message
    parser_.emplace();

    // Apply a reasonable limit to the allowed size
    // of the body in bytes to prevent abuse
    parser_->body_limit(10000);

    // Set the timeout
    stream_.expires_after(std::chrono::seconds(10));

    // Read a request
    http::async_read(
        stream_,
        buffer_,
        parser_->get(),
        beast::bind_front_handler(
            &http_session::on_read,
            shared_from_this()));
}

void http_session::on_read(beast::error_code ec, std::size_t)
{
    // This means they closed the connection
    if (ec == http::error::end_of_stream)
        return do_close();

    // Handle the error, if any
    if (ec)
        return fail(ec, "read");

    handle_request(std::move(parser_->get()));
}

void http_session::send_payload(std::string const& payload)
{
    // Send response and close this http_session
    auto const req = parser_->get();
    http::response<http::string_body> res{ http::status::ok, req.version() };
    res.set(http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(http::field::content_type, "application/json");
    res.keep_alive(false);
    res.body() = payload;
    res.prepare_payload();

    return write(std::move(res));
}

template <class BodyType>
void http_session::write(http::response<BodyType>&& res)
{
    // The lifetime of the message has to extend
    // for the duration of the async operation so
    // we use a shared_ptr to manage it.
    using response_type = typename std::decay<decltype(res)>::type;
    auto sp = boost::make_shared<response_type>(std::forward<decltype(res)>(res));

    // Write the response
    auto self = shared_from_this();
    http::async_write(stream_, *sp,
        [self, sp](beast::error_code ec, std::size_t bytes)
        {
            self->on_write(ec, bytes, sp->need_eof());
        });
}

void http_session::on_write(beast::error_code ec, std::size_t, bool close)
{
    // Handle the error, if any
    if (ec)
        return fail(ec, "write");

    if (close)
        return do_close();

    // Read another request
    do_read();
}

void http_session::do_close()
{
    // Send a TCP shutdown
    beast::error_code ec;
    stream_.socket().shutdown(tcp::socket::shutdown_send, ec);
}