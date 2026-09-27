#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "bupt_volumetric/config/app_config.h"
#include "bupt_volumetric/signaling/tcp_transport.h"

namespace bupt::volumetric {
namespace {

}  // namespace
}  // namespace bupt::volumetric

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  const bupt::volumetric::ProcessArgsResult parsed =
      bupt::volumetric::ParseProcessArgs(args);
  if (!parsed.ok) {
    std::cerr << "error: " << parsed.error << "\n";
    return 1;
  }
  if (parsed.options.role != bupt::volumetric::ProcessRole::kSignalingServer) {
    std::cerr << "error: signaling server requires --role=signaling\n";
    return 1;
  }

  bupt::volumetric::TcpSignalingServer server;
  if (!server.Start(parsed.options.signaling_port)) {
    std::cerr << "error: failed to bind signaling server on port "
              << parsed.options.signaling_port << "\n";
    return 1;
  }
  std::cout << "signaling server listening on 127.0.0.1:" << server.port()
            << "\n";
  server.Run();
  server.Stop();
  return 0;
}
