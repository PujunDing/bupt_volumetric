#ifndef BUPT_VOLUMETRIC_SIGNALING_TCP_TRANSPORT_H_
#define BUPT_VOLUMETRIC_SIGNALING_TCP_TRANSPORT_H_

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "bupt_volumetric/signaling/signaling_server.h"

namespace bupt::volumetric {

// Minimal blocking TCP transport for signaling. Frames are self-delimited by
// the signaling protocol envelope, so no extra length prefix is needed on the
// wire. The server routes between two named peers; a client connects, sends,
// and receives frames. Signaling is low-frequency, so blocking I/O with short
// poll timeouts is sufficient and keeps the code synchronous and auditable.

class TcpSignalingServer final {
 public:
  TcpSignalingServer();
  ~TcpSignalingServer();

  TcpSignalingServer(const TcpSignalingServer&) = delete;
  TcpSignalingServer& operator=(const TcpSignalingServer&) = delete;

  // Binds and listens on 127.0.0.1:port. Returns false on failure.
  bool Start(std::uint16_t port);

  // Blocks: accepts up to two peers, routes frames between them through the
  // internal SignalingServer, until Stop() is called or both peers disconnect.
  // Returns false only on a socket error during setup.
  bool Run();

  // Closes every socket and unblocks Run().
  void Stop();

  [[nodiscard]] std::uint16_t port() const noexcept;
  [[nodiscard]] std::uint64_t routed_count() const noexcept;
  [[nodiscard]] const SignalingServer& router() const noexcept;

 private:
  class Impl;
  Impl* impl_ = nullptr;
};

class TcpSignalingClient final {
 public:
  TcpSignalingClient();
  ~TcpSignalingClient();

  TcpSignalingClient(const TcpSignalingClient&) = delete;
  TcpSignalingClient& operator=(const TcpSignalingClient&) = delete;

  bool Connect(const std::string& host, std::uint16_t port);
  bool Send(const std::vector<std::uint8_t>& frame);
  // Reads one complete frame, blocking up to timeout_ms. Nullopt on timeout,
  // disconnect, or error.
  std::optional<std::vector<std::uint8_t>> Receive(int timeout_ms);
  void Close();
  [[nodiscard]] bool connected() const noexcept;

 private:
  class Impl;
  Impl* impl_ = nullptr;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_SIGNALING_TCP_TRANSPORT_H_
