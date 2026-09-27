#include <chrono>
#include <cstdint>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <windows.h>

#include "bupt_volumetric/apps/metrics_window.h"
#include "bupt_volumetric/apps/signal_loopback.h"
#include "bupt_volumetric/config/app_config.h"
#include "bupt_volumetric/latency/latency_recorder.h"
#include "bupt_volumetric/metrics/run_manifest.h"
#include "bupt_volumetric/signaling/protocol.h"
#include "bupt_volumetric/signaling/tcp_transport.h"

namespace {

constexpr const char* kRemotePeerId = "sender";

std::vector<std::uint8_t> RegisterFrame(const std::string& peer_id) {
  const auto frame = bupt::volumetric::SerializeSignalingMessage(
      bupt::volumetric::SignalingMessage{
          .type = bupt::volumetric::SignalingMessageType::kRegister,
          .from_peer_id = peer_id,
          .to_peer_id = "",
          .payload = ""});
  return frame.value_or(std::vector<std::uint8_t>{});
}

std::string BuildOverlay(const bupt::volumetric::LatencyRecorder& latency,
                         const bupt::volumetric::PeerConnectionSession& session,
                         double avg_blocks_per_second,
                         double instant_blocks_per_second) {
  const bupt::volumetric::TransportFeedbackSnapshot snapshot =
      session.transport_feedback();
  std::string text = latency.FormatOverlay() + "\n";
  text += "completed_frames=" + std::to_string(session.completed_frame_count()) +
          "  completed_blocks=" +
          std::to_string(session.completed_block_count()) + "\n";
  text += "fps_avg=" + std::to_string(avg_blocks_per_second) +
          "  fps_1s=" + std::to_string(instant_blocks_per_second) + " fps";
  if (snapshot.loss_fraction.has_value()) {
    text += "  loss=" + std::to_string(*snapshot.loss_fraction);
  } else {
    text += "  loss=n/a";
  }
  text += "\njitter_ms=" + std::to_string(snapshot.jitter_ms.value_or(0));
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
  if (parsed.options.role != bupt::volumetric::ProcessRole::kReceiver) {
    std::cerr << "error: receiver requires --role=receiver\n";
    return 1;
  }

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

  bupt::volumetric::MetricsWindow window;
  if (!window.Create("BUPT-Volumetric Receiver (latency)", 640, 360)) {
    std::cerr << "error: failed to create metrics window\n";
    return 1;
  }
  bupt::volumetric::LatencyRecorder latency;

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
      bupt::volumetric::MediaDirection::kReceiveOnly, /*is_offerer=*/false,
      /*timeout_ms=*/15000);
  if (!session) {
    std::cerr << "error: signaling loopback did not connect\n";
    return 1;
  }
  std::cerr << "receiver: signaling connected\n";

  // Sample transport stats, record the RTT as the latency signal, and refresh
  // the overlay until the window is closed.
  const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  std::deque<std::pair<double, std::uint64_t>> completion_history;
  bool running = true;
  while (running) {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        running = false;
        break;
      }
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }
    if (!running) {
      break;
    }

    session->CollectTransportStats();
    const std::uint64_t completed = session->completed_frame_count();
    // Record every block's end-to-end latency: the decoder queues one sample
    // per decoded block, so draining captures all of them (not just the latest).
    for (const std::uint64_t latency_us : session->DrainLatencyUs()) {
      latency.Record(static_cast<double>(latency_us) / 1000.0);
    }
    // Before the first block completes, fall back to the transport RTT so the
    // window still shows something.
    if (completed == 0) {
      const bupt::volumetric::TransportFeedbackSnapshot snapshot =
          session->transport_feedback();
      if (snapshot.selected_pair_rtt_ms.has_value()) {
        latency.Record(*snapshot.selected_pair_rtt_ms);
      }
    }
    const double elapsed_seconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - start)
            .count();

    // Average rate over the whole run.
    const double avg_blocks_per_second =
        elapsed_seconds > 0
            ? static_cast<double>(completed) / elapsed_seconds
            : 0.0;
    // Instantaneous rate over a rolling 1-second window.
    completion_history.emplace_back(elapsed_seconds, completed);
    while (!completion_history.empty() &&
           elapsed_seconds - completion_history.front().first > 1.0) {
      completion_history.pop_front();
    }
    double instant_blocks_per_second = 0.0;
    if (completion_history.size() >= 2) {
      const double window_seconds =
          elapsed_seconds - completion_history.front().first;
      const std::uint64_t window_blocks =
          completed - completion_history.front().second;
      if (window_seconds > 0) {
        instant_blocks_per_second =
            static_cast<double>(window_blocks) / window_seconds;
      }
    }

    window.DrawText(BuildOverlay(latency, *session, avg_blocks_per_second,
                                 instant_blocks_per_second));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  session->Stop();
  transport.Close();
  window.Destroy();
  std::cerr << "receiver: stopped\n";
  return 0;
}
