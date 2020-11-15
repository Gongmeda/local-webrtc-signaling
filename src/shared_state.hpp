#pragma once

#include <memory>
#include <mutex>
#include <unordered_set>

// Forward declaration
class webrtc_session;

// Represents the shared server state
class shared_state
{
    // Document root path for general http requests
    std::string const doc_root_;

    // This mutex synchronizes all access to shared_state
    std::mutex mutex_;

    // webrtc_session pointer
    std::shared_ptr<webrtc_session> webrtc_session_;

    // webrtc connection state
    bool answer_ready_state;
    std::string answer_payload_;

public:
    explicit shared_state(std::string doc_root);

    auto doc_root() { return doc_root_; }
    
    bool is_answer_ready();
    void set_answer_ready_state(bool state);
    std::string get_answer_payload();
    void set_answer_payload(std::string const& payload);
    void create_session(std::shared_ptr<shared_state> const& state);
    void create_connection(std::string const& offer_message);
};