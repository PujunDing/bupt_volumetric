#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "bupt_volumetric/apps/metrics_window.h"
#include "bupt_volumetric/apps/signal_loopback.h"
#include "bupt_volumetric/config/app_config.h"
#include "bupt_volumetric/metrics/run_manifest.h"
#include "bupt_volumetric/signaling/protocol.h"
#include "bupt_volumetric/signaling/tcp_transport.h"
#include "bupt_volumetric/simulation/frame_generator.h"

namespace {

constexpr const char* kRemotePeerId = "receiver";

std::vector<std::uint8_t> RegisterFrame(const std::string& peer_id) {
  const auto frame = bupt::volumetric::SerializeSignalingMessage(
      bupt::volumetric::SignalingMessage{
          .type = bupt::volumetric::SignalingMessageType::kRegister,
          .from_peer_id = peer_id,
          .to_peer_id = "",
          .payload = ""});
  return frame.value_or(std::vector<std::uint8_t>{});
}

std::string BuildSenderOverlay(const bupt::volumetric::PeerConnectionSession& session,
                               double actual_fps,
                               std::uint64_t sent_frames) {
  const bupt::volumetric::TransportFeedbackSnapshot snapshot =
      session.transport_feedback();
  std::string text = "target_bitrate=" +
                     std::to_string(snapshot.target_bitrate_bps) + " bps\n";
  text += "fps=" + std::to_string(actual_fps) +
          "  frames_sent=" + std::to_string(sent_frames) + "\n";
  text += "fec_sent=" + std::to_string(snapshot.fec_packets_sent.value_or(0)) +
          " pkts  " + std::to_string(snapshot.fec_bytes_sent.value_or(0)) +
          " bytes\n";
  text += "outbound=" + std::to_string(snapshot.outbound_packets.value_or(0)) +
          " pkts  pacer=" + std::to_string(snapshot.pacer_delay_ms.value_or(0)) +
          " ms\n";
  text += "send_delay=" +
          std::to_string(snapshot.total_packet_send_delay_ms.value_or(0)) +
          " ms  rtt=" + std::to_string(snapshot.selected_pair_rtt_ms.value_or(0)) +
          " ms";
  return text;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  const bupt::volumetric::ProcessArgsResult parsed =
      bupt::volumetric::ParseProcessArgs(args);
  if (!parsed.ok) {
    std::cerr << "error: " << parsed.error << "\n";
    return 1;
  }
  if (parsed.options.role != bupt::volumetric::ProcessRole::kSender) {
    std::cerr << "error: sender requires --role=sender\n";
    return 1;
  }

  // Build an immutable run manifest before any network or media activity.
  const std::string config_bytes =
      "run-id=" + parsed.options.run_id + ";seed=" +
      std::to_string(parsed.options.seed);
  const auto manifest = bupt::volumetric::BuildRunManifest(
      parsed.options.run_id,
      std::vector<std::uint8_t>(config_bytes.begin(), config_bytes.end()),
      parsed.options.seed, "localhost", "localhost-host-candidate",
      "clang-cl Release");
  if (!manifest.has_value()) {
    std::cerr << "error: failed to build run manifest\n";
    return 1;
  }

  bupt::volumetric::TcpSignalingClient transport;
  if (!transport.Connect(parsed.options.signaling_host,
                         parsed.options.signaling_port)) {
    std::cerr << "error: failed to connect to signaling server\n";
    return 1;
  }
  if (!transport.Send(RegisterFrame(parsed.options.peer_id))) {
    std::cerr << "error: failed to register with signaling server\n";
    return 1;
  }

  auto session = bupt::volumetric::RunSignalLoopback(
      transport, parsed.options.peer_id, kRemotePeerId,
      bupt::volumetric::MediaDirection::kSendOnly, /*is_offerer=*/true,
      /*timeout_ms=*/15000, parsed.options.bootstrap_bitrate_bps);
  if (!session) {
    std::cerr << "error: signaling loopback did not connect\n";
    return 1;
  }
  std::cerr << "sender: signaling connected\n";

  bupt::volumetric::MetricsWindow window;
  if (!window.Create("BUPT-Volumetric Sender (metrics)", 480, 300)) {
    std::cerr << "error: failed to create sender window\n";
    return 1;
  }

  const auto send_start = std::chrono::steady_clock::now();
  const bupt::volumetric::SimulatedFrameGenerator generator;
  std::uint64_t sent_frames = 0;
  bool sending = true;
  while (true) {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        sending = false;
        window.Destroy();
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if (!sending) {
      break;
    }

    if (sent_frames < parsed.options.frame_count) {
      if (session
              ->SubmitSimulatedFrame(
                  generator.Generate(sent_frames + 1, manifest->seed),
                  static_cast<std::uint32_t>((sent_frames + 1) * 3'000))
              .ok) {
        ++sent_frames;
      }
    }

    session->CollectTransportStats();
    const double elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      send_start)
            .count();
    const double actual_fps =
        elapsed_seconds > 0 ? static_cast<double>(sent_frames) / elapsed_seconds
                            : 0.0;
    window.DrawText(BuildSenderOverlay(*session, actual_fps, sent_frames));

    if (sent_frames >= parsed.options.frame_count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    } else {
      std::this_thread::sleep_for(
          std::chrono::milliseconds(parsed.options.frame_interval_ms));
    }
  }

  session->CollectTransportStats();
  const bupt::volumetric::TransportFeedbackSnapshot snapshot =
      session->transport_feedback();
  std::cerr << "sender: summary {\n"
            << "  rtt_ms=" << snapshot.selected_pair_rtt_ms.value_or(0) << "\n"
            << "  target_bitrate_bps=" << snapshot.target_bitrate_bps << "\n"
            << "  frames_sent=" << sent_frames << "\n"
            << "  outbound_packets=" << snapshot.outbound_packets.value_or(0)
            << " outbound_bytes=" << snapshot.outbound_bytes.value_or(0) << "\n"
            << "  fec_packets_sent=" << snapshot.fec_packets_sent.value_or(0)
            << " fec_bytes_sent=" << snapshot.fec_bytes_sent.value_or(0) << "\n"
            << "  pacer_delay_ms=" << snapshot.pacer_delay_ms.value_or(0) << "\n"
            << "  total_packet_send_delay_ms="
            << snapshot.total_packet_send_delay_ms.value_or(0) << "\n"
            << "}\n";
  session->Stop();
  transport.Close();
  return 0;
}
