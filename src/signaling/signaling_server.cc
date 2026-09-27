#include "bupt_volumetric/signaling/signaling_server.h"

#include <cstdint>
#include <string>
#include <utility>

namespace bupt::volumetric {

SignalingServerResult SignalingServer::Register(const std::string& peer_id) {
  if (shutting_down_) {
    return {.status = SignalingServerStatus::kShuttingDown,
            .message = "server is shutting down"};
  }
  if (peer_id.empty()) {
    return {.status = SignalingServerStatus::kDuplicatePeer,
            .message = "peer id must not be empty"};
  }
  if (connected_.contains(peer_id)) {
    return {.status = SignalingServerStatus::kDuplicatePeer,
            .message = "peer id already connected"};
  }
  connected_.insert(peer_id);
  queues_.emplace(peer_id, std::deque<SignalingMessage>{});
  return {.status = SignalingServerStatus::kOk, .message = ""};
}

SignalingServerResult SignalingServer::Route(const SignalingMessage& message) {
  if (shutting_down_) {
    return {.status = SignalingServerStatus::kShuttingDown,
            .message = "server is shutting down"};
  }
  if (message.to_peer_id.empty()) {
    return {.status = SignalingServerStatus::kNoTargetPeer,
            .message = "message has no target peer"};
  }
  const auto queue = queues_.find(message.to_peer_id);
  if (queue == queues_.end()) {
    return {.status = SignalingServerStatus::kUnknownPeer,
            .message = "target peer is not connected"};
  }
  queue->second.push_back(message);
  ++routed_message_count_;
  return {.status = SignalingServerStatus::kOk, .message = ""};
}

std::vector<SignalingMessage> SignalingServer::Poll(const std::string& peer_id) {
  const auto queue = queues_.find(peer_id);
  if (queue == queues_.end()) {
    return {};
  }
  std::vector<SignalingMessage> messages;
  messages.reserve(queue->second.size());
  while (!queue->second.empty()) {
    messages.push_back(std::move(queue->second.front()));
    queue->second.pop_front();
  }
  return messages;
}

SignalingServerResult SignalingServer::Disconnect(const std::string& peer_id) {
  if (shutting_down_) {
    return {.status = SignalingServerStatus::kShuttingDown,
            .message = "server is shutting down"};
  }
  if (connected_.erase(peer_id) == 0) {
    return {.status = SignalingServerStatus::kUnknownPeer,
            .message = "peer is not connected"};
  }
  queues_.erase(peer_id);
  return {.status = SignalingServerStatus::kOk, .message = ""};
}

void SignalingServer::Shutdown() {
  shutting_down_ = true;
  connected_.clear();
  queues_.clear();
}

std::size_t SignalingServer::connected_peer_count() const noexcept {
  return connected_.size();
}

bool SignalingServer::IsConnected(const std::string& peer_id) const {
  return connected_.contains(peer_id);
}

std::uint64_t SignalingServer::routed_message_count() const noexcept {
  return routed_message_count_;
}

}  // namespace bupt::volumetric
