#include "bupt_volumetric/apps/signal_loopback.h"

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "bupt_volumetric/signaling/protocol.h"

namespace bupt::volumetric {
namespace {

struct Pending {
  std::mutex mutex;
  std::deque<SignalingMessage> outbound;
};

void EnqueueOutbound(Pending& pending, SignalingMessage message) {
  std::lock_guard<std::mutex> lock(pending.mutex);
  pending.outbound.push_back(std::move(message));
}

std::optional<SignalingMessage> PopOutbound(Pending& pending) {
  std::lock_guard<std::mutex> lock(pending.mutex);
  if (pending.outbound.empty()) {
    return std::nullopt;
  }
  SignalingMessage message = std::move(pending.outbound.front());
  pending.outbound.pop_front();
  return message;
}

}  // namespace

std::unique_ptr<PeerConnectionSession> RunSignalLoopback(
    TcpSignalingClient& transport,
    const std::string& peer_id,
    const std::string& remote_peer_id,
    MediaDirection direction,
    bool is_offerer,
    int timeout_ms,
    std::uint64_t bootstrap_bitrate_bps) {
  auto pending = std::make_shared<Pending>();

  PeerConnectionCallbacks callbacks;
  callbacks.on_local_description =
      [pending, peer_id, remote_peer_id](std::string type, std::string sdp) {
        EnqueueOutbound(
            *pending,
            SignalingMessage{.type = type == "offer"
                                        ? SignalingMessageType::kOffer
                                        : SignalingMessageType::kAnswer,
                             .from_peer_id = peer_id,
                             .to_peer_id = remote_peer_id,
                             .payload = std::move(sdp)});
      };
  callbacks.on_local_ice_candidate =
      [pending, peer_id, remote_peer_id](std::string mid, int mline,
                                         std::string candidate) {
        EnqueueOutbound(
            *pending,
            SignalingMessage{.type = SignalingMessageType::kIceCandidate,
                             .from_peer_id = peer_id,
                             .to_peer_id = remote_peer_id,
                             .payload = mid + "\n" + std::to_string(mline) +
                                        "\n" + std::move(candidate)});
      };
  callbacks.on_state = [](PeerSessionState) {};

  std::unique_ptr<PeerConnectionSession> session =
      CreatePeerConnectionSession(std::move(callbacks));
  RtcSessionConfig config;
  config.direction = direction;
  config.bootstrap_bitrate_bps = bootstrap_bitrate_bps;
  if (!session->Start(config).ok) {
    return nullptr;
  }
  if (is_offerer) {
    if (!session->CreateOffer().ok) {
      return nullptr;
    }
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);
  while (std::chrono::steady_clock::now() < deadline) {
    for (;;) {
      auto message = PopOutbound(*pending);
      if (!message.has_value()) {
        break;
      }
      auto frame = SerializeSignalingMessage(*message);
      if (!frame.has_value() || !transport.Send(*frame)) {
        return nullptr;
      }
    }
    for (;;) {
      auto frame = transport.Receive(50);
      if (!frame.has_value()) {
        break;
      }
      auto message = ParseSignalingMessage(*frame);
      if (!message.has_value()) {
        continue;
      }
      switch (message->type) {
        case SignalingMessageType::kOffer:
          session->ApplyRemoteDescription("offer", message->payload);
          session->CreateAnswer();
          break;
        case SignalingMessageType::kAnswer:
          session->ApplyRemoteDescription("answer", message->payload);
          break;
        case SignalingMessageType::kIceCandidate: {
          const std::string& p = message->payload;
          const std::size_t nl1 = p.find('\n');
          const std::size_t nl2 = p.find('\n', nl1 + 1);
          if (nl1 != std::string::npos && nl2 != std::string::npos) {
            session->AddRemoteIceCandidate(
                p.substr(0, nl1),
                std::stoi(p.substr(nl1 + 1, nl2 - nl1 - 1)), p.substr(nl2 + 1));
          }
          break;
        }
        default:
          break;
      }
    }
    if (session->dtls_connected()) {
      return session;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return session->dtls_connected() ? std::move(session) : nullptr;
}

}  // namespace bupt::volumetric
