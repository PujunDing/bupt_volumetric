#include "bupt_volumetric/config/app_config.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace bupt::volumetric {
namespace {

// Splits "--key=value" into (key, value). Returns nullopt on malformed input.
std::optional<std::pair<std::string, std::string>> SplitFlag(
    std::string_view arg) {
  if (arg.size() < 3 || !arg.starts_with("--")) {
    return std::nullopt;
  }
  const std::size_t eq = arg.find('=');
  if (eq == std::string_view::npos) {
    return std::nullopt;
  }
  return std::make_pair(std::string(arg.substr(2, eq - 2)),
                        std::string(arg.substr(eq + 1)));
}

std::optional<ProcessRole> ParseRole(std::string_view role) {
  if (role == "sender") {
    return ProcessRole::kSender;
  }
  if (role == "receiver") {
    return ProcessRole::kReceiver;
  }
  if (role == "signaling") {
    return ProcessRole::kSignalingServer;
  }
  return std::nullopt;
}

}  // namespace

ProcessArgsResult ParseProcessArgs(const std::vector<std::string>& args) {
  ProcessOptions options;
  bool role_seen = false;

  for (const std::string& arg : args) {
    const auto flag = SplitFlag(arg);
    if (!flag.has_value()) {
      return ProcessArgsResult::Error("unknown or malformed argument: " + arg);
    }
    const std::string& key = flag->first;
    const std::string& value = flag->second;

    if (key == "run-id") {
      options.run_id = value;
    } else if (key == "result-dir") {
      options.result_dir = value;
    } else if (key == "config") {
      options.config_path = value;
    } else if (key == "role") {
      const auto role = ParseRole(value);
      if (!role.has_value()) {
        return ProcessArgsResult::Error("invalid role: " + value);
      }
      options.role = *role;
      role_seen = true;
    } else if (key == "peer-id") {
      options.peer_id = value;
    } else if (key == "signaling-host") {
      options.signaling_host = value;
    } else if (key == "signaling-port") {
      try {
        const int port = std::stoi(value);
        if (port < 0 || port > 65535) {
          return ProcessArgsResult::Error("signaling-port out of range: " +
                                          value);
        }
        options.signaling_port = static_cast<std::uint16_t>(port);
      } catch (...) {
        return ProcessArgsResult::Error("invalid signaling-port: " + value);
      }
    } else if (key == "seed") {
      try {
        options.seed = std::stoull(value);
      } catch (...) {
        return ProcessArgsResult::Error("invalid seed: " + value);
      }
    } else if (key == "frames") {
      try {
        options.frame_count = std::stoull(value);
      } catch (...) {
        return ProcessArgsResult::Error("invalid frames: " + value);
      }
    } else if (key == "fps") {
      try {
        const double fps = std::stod(value);
        if (fps <= 0) {
          return ProcessArgsResult::Error("fps must be positive: " + value);
        }
        options.frame_interval_ms = static_cast<std::uint32_t>(1000.0 / fps);
      } catch (...) {
        return ProcessArgsResult::Error("invalid fps: " + value);
      }
    } else if (key == "bitrate") {
      try {
        options.bootstrap_bitrate_bps = std::stoull(value);
      } catch (...) {
        return ProcessArgsResult::Error("invalid bitrate: " + value);
      }
    } else {
      return ProcessArgsResult::Error("unknown flag: --" + key);
    }
  }

  if (options.run_id.empty()) {
    return ProcessArgsResult::Error("--run-id is required");
  }
  if (options.result_dir.empty()) {
    return ProcessArgsResult::Error("--result-dir is required");
  }
  if (!role_seen) {
    return ProcessArgsResult::Error("--role is required");
  }
  if (options.role != ProcessRole::kSignalingServer && options.peer_id.empty()) {
    return ProcessArgsResult::Error("--peer-id is required for sender/receiver");
  }

  ProcessArgsResult result;
  result.options = std::move(options);
  return result;
}

}  // namespace bupt::volumetric
