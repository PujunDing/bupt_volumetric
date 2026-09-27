#include "bupt_volumetric/signaling/tcp_transport.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace bupt::volumetric {
namespace {

// One process-lifetime Winsock owner, mirroring the WebRTC runtime contract.
class WinsockRuntime {
 public:
  WinsockRuntime() {
    WSADATA data;
    ok_ = (WSAStartup(MAKEWORD(2, 2), &data) == 0);
  }
  ~WinsockRuntime() {
    if (ok_) {
      WSACleanup();
    }
  }
  bool ok() const { return ok_; }

 private:
  bool ok_ = false;
};

WinsockRuntime& Runtime() {
  static WinsockRuntime runtime;
  return runtime;
}

// Reads exactly `count` bytes, returning false on EOF/error.
bool ReadAll(SOCKET socket, std::uint8_t* buffer, std::size_t count) {
  std::size_t offset = 0;
  while (offset < count) {
    const int received = ::recv(socket, reinterpret_cast<char*>(buffer + offset),
                                static_cast<int>(count - offset), 0);
    if (received <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(received);
  }
  return true;
}

bool WriteAll(SOCKET socket, const std::uint8_t* buffer, std::size_t count) {
  std::size_t offset = 0;
  while (offset < count) {
    const int sent = ::send(socket,
                            reinterpret_cast<const char*>(buffer + offset),
                            static_cast<int>(count - offset), 0);
    if (sent <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(sent);
  }
  return true;
}

constexpr std::size_t kHeaderBytes = 12;

// Reads one self-delimiting protocol frame. Nullopt on EOF/error.
std::optional<std::vector<std::uint8_t>> ReadFrame(SOCKET socket) {
  std::uint8_t header[kHeaderBytes];
  if (!ReadAll(socket, header, kHeaderBytes)) {
    return std::nullopt;
  }
  const std::size_t from_len =
      (static_cast<std::size_t>(header[4]) << 8) | header[5];
  const std::size_t to_len =
      (static_cast<std::size_t>(header[6]) << 8) | header[7];
  const std::size_t payload_len =
      (static_cast<std::size_t>(header[8]) << 24) |
      (static_cast<std::size_t>(header[9]) << 16) |
      (static_cast<std::size_t>(header[10]) << 8) | header[11];
  const std::size_t body_len = from_len + to_len + payload_len;
  std::vector<std::uint8_t> frame;
  frame.reserve(kHeaderBytes + body_len);
  frame.assign(header, header + kHeaderBytes);
  const std::size_t old_size = frame.size();
  frame.resize(old_size + body_len);
  if (!ReadAll(socket, frame.data() + old_size, body_len)) {
    return std::nullopt;
  }
  return frame;
}

}  // namespace

class TcpSignalingServer::Impl {
 public:
  ~Impl() { Stop(); }

  bool Start(std::uint16_t requested_port) {
    if (!Runtime().ok()) {
      return false;
    }
    listen_socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket_ == INVALID_SOCKET) {
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(requested_port);
    if (::bind(listen_socket_, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listen_socket_, 2) != 0) {
      ::closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
      return false;
    }
    sockaddr_in bound{};
    int length = sizeof(bound);
    if (::getsockname(listen_socket_, reinterpret_cast<sockaddr*>(&bound),
                      &length) == 0) {
      port_ = ntohs(bound.sin_port);
    } else {
      port_ = requested_port;
    }
    return true;
  }

  bool Run() {
    if (listen_socket_ == INVALID_SOCKET) {
      return false;
    }
    // Accept up to two peers with a short timeout so Stop() can interrupt.
    for (int peer = 0; peer < 2; ++peer) {
      for (;;) {
        if (stopping_) {
          return true;
        }
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(listen_socket_, &read_set);
        timeval timeout{0, 100'000};  // 100 ms
        const int ready =
            ::select(0, &read_set, nullptr, nullptr, &timeout);
        if (ready <= 0) {
          continue;
        }
        SOCKET client = ::accept(listen_socket_, nullptr, nullptr);
        if (client != INVALID_SOCKET) {
          peer_sockets_[peer] = client;
        }
        break;
      }
    }

    // Route frames between the two peers until Stop() or both disconnect.
    while (!stopping_) {
      fd_set read_set;
      FD_ZERO(&read_set);
      int max_socket = 0;
      bool any = false;
      for (SOCKET socket : peer_sockets_) {
        if (socket != INVALID_SOCKET) {
          FD_SET(socket, &read_set);
          if (static_cast<int>(socket) > max_socket) {
            max_socket = static_cast<int>(socket);
          }
          any = true;
        }
      }
      if (!any) {
        break;
      }
      timeval timeout{0, 100'000};
      const int ready = ::select(max_socket + 1, &read_set, nullptr, nullptr,
                                 &timeout);
      if (ready <= 0) {
        continue;
      }
      for (int peer = 0; peer < 2; ++peer) {
        SOCKET socket = peer_sockets_[peer];
        if (socket == INVALID_SOCKET || !FD_ISSET(socket, &read_set)) {
          continue;
        }
        auto frame = ReadFrame(socket);
        if (!frame.has_value()) {
          ::closesocket(socket);
          peer_sockets_[peer] = INVALID_SOCKET;
          peer_ids_[peer].clear();
          continue;
        }
        auto message = ParseSignalingMessage(*frame);
        if (!message.has_value()) {
          continue;
        }
        if (message->type == SignalingMessageType::kRegister) {
          peer_ids_[peer] = message->from_peer_id;
          router_.Register(message->from_peer_id);
        } else {
          router_.Route(*message);
        }
      }
      FlushQueues();
    }
    return true;
  }

  void Stop() {
    stopping_ = true;
    for (SOCKET& socket : peer_sockets_) {
      if (socket != INVALID_SOCKET) {
        ::closesocket(socket);
        socket = INVALID_SOCKET;
      }
    }
    if (listen_socket_ != INVALID_SOCKET) {
      ::closesocket(listen_socket_);
      listen_socket_ = INVALID_SOCKET;
    }
    router_.Shutdown();
  }

  std::uint16_t port() const { return port_; }
  std::uint64_t routed_count() const { return router_.routed_message_count(); }
  const SignalingServer& router() const { return router_; }

 private:
  void FlushQueues() {
    for (int peer = 0; peer < 2; ++peer) {
      if (peer_ids_[peer].empty() || peer_sockets_[peer] == INVALID_SOCKET) {
        continue;
      }
      for (const SignalingMessage& message : router_.Poll(peer_ids_[peer])) {
        auto frame = SerializeSignalingMessage(message);
        if (frame.has_value()) {
          WriteAll(peer_sockets_[peer], frame->data(), frame->size());
        }
      }
    }
  }

  SOCKET listen_socket_ = INVALID_SOCKET;
  SOCKET peer_sockets_[2] = {INVALID_SOCKET, INVALID_SOCKET};
  std::string peer_ids_[2];
  SignalingServer router_;
  std::uint16_t port_ = 0;
  std::atomic<bool> stopping_{false};
};

class TcpSignalingClient::Impl {
 public:
  ~Impl() { Close(); }

  bool Connect(const std::string& host, std::uint16_t port) {
    if (!Runtime().ok()) {
      return false;
    }
    socket_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) {
      return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
      ::closesocket(socket_);
      socket_ = INVALID_SOCKET;
      return false;
    }
    if (::connect(socket_, reinterpret_cast<sockaddr*>(&address),
                  sizeof(address)) != 0) {
      ::closesocket(socket_);
      socket_ = INVALID_SOCKET;
      return false;
    }
    connected_ = true;
    return true;
  }

  bool Send(const std::vector<std::uint8_t>& frame) {
    if (!connected_) {
      return false;
    }
    return WriteAll(socket_, frame.data(), frame.size());
  }

  std::optional<std::vector<std::uint8_t>> Receive(int timeout_ms) {
    if (!connected_) {
      return std::nullopt;
    }
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(socket_, &read_set);
    timeval timeout{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    const int ready = ::select(0, &read_set, nullptr, nullptr, &timeout);
    if (ready <= 0) {
      return std::nullopt;
    }
    return ReadFrame(socket_);
  }

  void Close() {
    connected_ = false;
    if (socket_ != INVALID_SOCKET) {
      ::closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  bool connected() const { return connected_; }

 private:
  SOCKET socket_ = INVALID_SOCKET;
  std::atomic<bool> connected_{false};
};

TcpSignalingServer::TcpSignalingServer() : impl_(new Impl()) {}
TcpSignalingServer::~TcpSignalingServer() { delete impl_; }
bool TcpSignalingServer::Start(std::uint16_t port) { return impl_->Start(port); }
bool TcpSignalingServer::Run() { return impl_->Run(); }
void TcpSignalingServer::Stop() { impl_->Stop(); }
std::uint16_t TcpSignalingServer::port() const noexcept { return impl_->port(); }
std::uint64_t TcpSignalingServer::routed_count() const noexcept {
  return impl_->routed_count();
}
const SignalingServer& TcpSignalingServer::router() const noexcept {
  return impl_->router();
}

TcpSignalingClient::TcpSignalingClient() : impl_(new Impl()) {}
TcpSignalingClient::~TcpSignalingClient() { delete impl_; }
bool TcpSignalingClient::Connect(const std::string& host, std::uint16_t port) {
  return impl_->Connect(host, port);
}
bool TcpSignalingClient::Send(const std::vector<std::uint8_t>& frame) {
  return impl_->Send(frame);
}
std::optional<std::vector<std::uint8_t>> TcpSignalingClient::Receive(
    int timeout_ms) {
  return impl_->Receive(timeout_ms);
}
void TcpSignalingClient::Close() { impl_->Close(); }
bool TcpSignalingClient::connected() const noexcept {
  return impl_->connected();
}

}  // namespace bupt::volumetric
