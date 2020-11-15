#include "shared_state.hpp"
#include "webrtc_session.hpp"

shared_state::shared_state(std::string doc_root)
    : doc_root_(doc_root)
    , answer_ready_state(false)
{
}

// Check if webrtc answer sdp is ready
bool shared_state::is_answer_ready()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return answer_ready_state;
}

// Set webrtc answer ready state
void shared_state::set_answer_ready_state(bool state)
{
    std::lock_guard<std::mutex> lock(mutex_);
    answer_ready_state = state;
}

// Get answer sdp payload
std::string shared_state::get_answer_payload()
{
    std::lock_guard<std::mutex> lock(mutex_);
    return answer_payload_;
}

// Set answer sdp payload
void shared_state::set_answer_payload(std::string const& message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    answer_payload_ = message;
}

// Create new webrtc_session in shared-state
void shared_state::create_session(std::shared_ptr<shared_state> const& state)
{
    std::lock_guard<std::mutex> lock(mutex_);
    // If webrtc_session is already created, do nothing
    if (!webrtc_session_)
        webrtc_session_ = std::make_shared<webrtc_session>(state);
}

// Create new webrtc connection in shared-state
void shared_state::create_connection(std::string const& offer_message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    webrtc_session_->create_connection(offer_message);
}