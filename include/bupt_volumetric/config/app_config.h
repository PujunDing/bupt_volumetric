#ifndef BUPT_VOLUMETRIC_CONFIG_APP_CONFIG_H_
#define BUPT_VOLUMETRIC_CONFIG_APP_CONFIG_H_

#include <cstdint>
#include <string>
#include <vector>

namespace bupt::volumetric {

// Which composition root a process executes.
enum class ProcessRole : std::uint8_t {
  kSender = 0,
  kReceiver = 1,
  kSignalingServer = 2,
};

// Validated command-line options shared by the three runnable processes.
// A run without a run id or result directory is not eligible for research
// comparison and is rejected before any network or WebRTC object is created.
struct ProcessOptions final {
  std::string run_id;
  std::string result_dir;
  std::string config_path;
  ProcessRole role = ProcessRole::kSender;
  std::string peer_id;
  std::uint64_t seed = 0;
  std::string signaling_host = "127.0.0.1";
  std::uint16_t signaling_port = 9090;
  // Sender-only run knobs: number of simulated frames, submission interval,
  // and the bootstrap bitrate used before GoogCC produces its first estimate.
  std::uint64_t frame_count = 30;
  std::uint32_t frame_interval_ms = 34;
  std::uint64_t bootstrap_bitrate_bps = 1'000'000;
};

struct ProcessArgsResult final {
  bool ok = true;
  std::string error;
  ProcessOptions options;

  static ProcessArgsResult Error(std::string message) {
    ProcessArgsResult result;
    result.ok = false;
    result.error = std::move(message);
    return result;
  }
};

// Parses `--run-id=`, `--result-dir=`, `--config=`, `--role=`, `--peer-id=`,
// and `--seed=` arguments. Fails closed on unknown flags, missing values, and
// invalid role names. `role` and `peer_id` default appropriately for a sender.
[[nodiscard]] ProcessArgsResult ParseProcessArgs(
    const std::vector<std::string>& args);

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_CONFIG_APP_CONFIG_H_
