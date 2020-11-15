#pragma once

#pragma comment(lib, "secur32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dmoguids.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "msdmo.lib")
#pragma comment(lib, "Strmiids.lib")

#include "webrtc_connection.hpp"

#include <string>

// WebRTC headers
#include <webrtc/rtc_base/ssladapter.h>
#include <webrtc/rtc_base/thread.h>
#include <webrtc/api/audio_codecs/builtin_audio_encoder_factory.h>
#include <webrtc/api/audio_codecs/builtin_audio_decoder_factory.h>

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

class webrtc_session
{
	std::shared_ptr<shared_state> state_;

	// webrtc
	std::unique_ptr<rtc::Thread> network_thread;
	std::unique_ptr<rtc::Thread> worker_thread;
	std::unique_ptr<rtc::Thread> signaling_thread;
	rtc::scoped_refptr<webrtc::AudioEncoderFactory> audio_encoder_factory;
	rtc::scoped_refptr<webrtc::AudioDecoderFactory> audio_decoder_factory;

	rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> peer_connection_factory;
	webrtc::PeerConnectionInterface::RTCConfiguration peer_connection_config;
	std::unique_ptr<webrtc_connection> connection;

public:
	// Constructor
	webrtc_session(std::shared_ptr<shared_state> const& state) : state_(state)
	{
		std::cout << std::this_thread::get_id() << ":"
			<< "Create webrtc_session" << std::endl;

		// Initialize
		rtc::InitializeSSL();
		rtc::InitRandom(rtc::Time());
		rtc::ThreadManager::Instance()->WrapCurrentThread();

		// Create threads
		network_thread = rtc::Thread::CreateWithSocketServer();
		network_thread->Start();
		worker_thread = rtc::Thread::Create();
		worker_thread->Start();
		signaling_thread = rtc::Thread::Create();
		signaling_thread->Start();

		// Create Audio Encoder/Decoder and Peer Connection Factories
		audio_encoder_factory = webrtc::CreateBuiltinAudioEncoderFactory();
		audio_decoder_factory = webrtc::CreateBuiltinAudioDecoderFactory();
		peer_connection_factory = webrtc::CreatePeerConnectionFactory(
			network_thread.get(),
			worker_thread.get(),
			signaling_thread.get(),
			nullptr,
			audio_encoder_factory,
			audio_decoder_factory,
			nullptr,
			nullptr);

		// If Peer Connection Factory creation failed, exit
		if (peer_connection_factory.get() == nullptr)
		{
			std::cout << std::this_thread::get_id() << ":"
				<< "Error on CreateModularPeerConnectionFactory." << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	~webrtc_session()
	{
		quit();
	}

	// Connection callback setters (used to set connection observers from outside)
	void on_ice_candidate(std::function<void(const webrtc::IceCandidateInterface* candidate)> f) { connection->on_ice_candidate = f; }
	void on_ice_connection_change(std::function<void(webrtc::PeerConnectionInterface::IceConnectionState)> f) { connection->on_ice_connection_change = f; }
	void on_ice_gathering_change(std::function<void(webrtc::PeerConnectionInterface::IceGatheringState)> f) { connection->on_ice_gathering_change = f; }
	void on_message(std::function<void(const std::string&)> f) { connection->on_message = f; }

	// Create new Peer Connection and Data Channel
	void create_connection(std::string const& offer_payload)
	{
		if (!connection)
			connection = std::make_unique<webrtc_connection>("uuid");

		// Set ICE gathering state change handler
		on_ice_gathering_change([this](webrtc::PeerConnectionInterface::IceGatheringState new_state)
			{
				std::cout << "PeerConnectionInterface::IceGatheringState : " << new_state << std::endl;

				// If gathering is finished, set answer payload and answer ready state (for sending answer message to remote peer in http_session)
				if (new_state == webrtc::PeerConnectionInterface::IceGatheringState::kIceGatheringComplete)
				{
					// Get local session description
					auto local_sdp = connection->peer_connection->local_description();

					// Add new ICE candidate to the sdp (one candidate for local connection)
					std::string sdp_str;
					local_sdp->ToString(&sdp_str);
					for (auto candidate : connection->cadidates_)
					{
						sdp_str.append("a=");
						sdp_str.append(candidate);
						sdp_str.append("\r\n");
					}

					// Create payload
					rapidjson::Document message_object;
					message_object.SetObject();
					message_object.AddMember("type", "answer", message_object.GetAllocator());
					message_object.AddMember("sdp", rapidjson::StringRef(sdp_str.c_str()), message_object.GetAllocator());
					rapidjson::StringBuffer strbuf;
					rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
					message_object.Accept(writer);
					std::string payload = strbuf.GetString();

					// Set answer payload, answer ready state
					state_->set_answer_payload(payload);
					state_->set_answer_ready_state(true);
				}
			});

		// Set new ICE candidates handler
		on_ice_candidate([this](const webrtc::IceCandidateInterface* candidate)
			{
				// Add new candidates to the contatiner in webrtc_connection
				std::string candidate_str;
				candidate->ToString(&candidate_str);
				connection->cadidates_.push_back(candidate_str);
			});

		// Set ICE state change(unexpected disconnection) handler
		on_ice_connection_change([this](webrtc::PeerConnectionInterface::IceConnectionState new_state)
			{
				switch (new_state)
				{
				case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionConnected:
				{
					std::cout << "IceConnectionState::kIceConnectionConnected" << std::endl;
					break;
				}
				case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionCompleted:
				{
					std::cout << "IceConnectionState::kIceConnectionCompleted" << std::endl;
					break;
				}
				case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionFailed:
				{
					std::cout << "IceConnectionState::kIceConnectionFailed" << std::endl;
					quit();
					break;
				}
				case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionDisconnected:
				{
					std::cout << "IceConnectionState::kIceConnectionDisconnected" << std::endl;
					connection->peer_connection->Close();
					break;
				}
				case webrtc::PeerConnectionInterface::IceConnectionState::kIceConnectionClosed:
				{
					std::cout << "IceConnectionState::kIceConnectionClosed" << std::endl;
					connection->cadidates_.clear();
					break;
				}
				}
			});

		// Set data channel message handler
		on_message([this](const std::string& received_message)
			{
				std::cout << "Data Channel : " << received_message << std::endl;
				std::string payload = received_message;
				webrtc::DataBuffer send_message(payload);
				if (connection->data_channel)
					connection->data_channel->Send(send_message);
			});

		// Create Peer Connection
		connection->peer_connection = peer_connection_factory
			->CreatePeerConnection(peer_connection_config, nullptr, nullptr, &connection->pco);
		
		// Create Data Channel
		webrtc::DataChannelInit data_channel_config;
		data_channel_config.ordered = false;
		data_channel_config.maxRetransmits = 0;
		connection->data_channel = connection->peer_connection->CreateDataChannel("dc", &data_channel_config);
		connection->data_channel->RegisterObserver(&connection->dco);
		
		// TODO : Local MediaStreamTracks & AddStream

		// Create Session Description and send it to remote peer
		webrtc::SdpParseError error;
		webrtc::SessionDescriptionInterface* session_description(
			webrtc::CreateSessionDescription("offer", offer_payload, &error));
		connection->peer_connection->SetRemoteDescription(connection->ssdo, session_description);
		connection->peer_connection->CreateAnswer(connection->csdo, webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
	}

    void quit() {
		connection->peer_connection = nullptr;
		connection->data_channel = nullptr;
        peer_connection_factory = nullptr;

        network_thread->Stop();
        worker_thread->Stop();
        signaling_thread->Stop();
    }
};