#pragma once

#include <string>
#include <functional>
#include <thread>
#include <iostream>

// WebRTC headers
#include <webrtc/api/peerconnectioninterface.h>

class webrtc_connection
{
public:
	// Connection name
	const std::string uuid_;

    //ICE candidates
    std::vector<std::string> cadidates_;

	// WebRTC connections;
	rtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection;
	rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel;

	// Callbacks
    std::function<void(const webrtc::IceCandidateInterface* candidate)> on_ice_candidate;
	std::function<void(webrtc::PeerConnectionInterface::IceConnectionState new_state)> on_ice_connection_change;
    std::function<void(webrtc::PeerConnectionInterface::IceGatheringState new_state)> on_ice_gathering_change;
	std::function<void(const std::string&)> on_message;

    // Observer classes
    class PCO : public webrtc::PeerConnectionObserver
    {
        webrtc_connection& parent;

    public:
        PCO(webrtc_connection& parent) : parent(parent) {}

        void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) override {}

        void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

        void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) override {}

        void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> data_channel) override
        {
            parent.data_channel = data_channel;
            parent.data_channel->RegisterObserver(&parent.dco);
        }

        void OnRenegotiationNeeded() override {}

        void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) override
        {
            if (parent.on_ice_connection_change)
                parent.on_ice_connection_change(new_state);
        }

        void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) override
        {
            if (parent.on_ice_gathering_change)
                parent.on_ice_gathering_change(new_state);
        }

        void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) override
        {
            if (parent.on_ice_candidate)
                parent.on_ice_candidate(candidate);
        }
    };

    class DCO : public webrtc::DataChannelObserver
    {
        webrtc_connection& parent;

    public:
        DCO(webrtc_connection& parent) : parent(parent) {}

        void OnStateChange() override {}
        
        void OnMessage(const webrtc::DataBuffer& buffer) override
        {
            if (parent.on_message)
                parent.on_message(std::string(buffer.data.data<char>(), buffer.data.size()));
        }

        void OnBufferedAmountChange(uint64_t previous_amount) override {}
    };

    class CSDO : public webrtc::CreateSessionDescriptionObserver
    {
        webrtc_connection& parent;

    public:
        CSDO(webrtc_connection& parent) : parent(parent) {}

        void OnSuccess(webrtc::SessionDescriptionInterface* desc) override
        {
            parent.peer_connection->SetLocalDescription(parent.ssdo, desc);
        }

        void OnFailure(const std::string& error) override {}
    };

    class SSDO : public webrtc::SetSessionDescriptionObserver
    {
    private:
        webrtc_connection& parent;

    public:
        SSDO(webrtc_connection& parent) : parent(parent) {}

        void OnSuccess() override {}

        void OnFailure(const std::string& error) override {}
    };

    // Observer objects
    PCO pco;
    DCO dco;
    rtc::scoped_refptr<CSDO> csdo;
    rtc::scoped_refptr<SSDO> ssdo;

    // Constructor
    webrtc_connection(const std::string& uuid) :
        uuid_(uuid),
        pco(*this),
        dco(*this),
        csdo(new rtc::RefCountedObject<CSDO>(*this)),
        ssdo(new rtc::RefCountedObject<SSDO>(*this))
    {
    }

    // Deconstructor
    ~webrtc_connection() {}
};