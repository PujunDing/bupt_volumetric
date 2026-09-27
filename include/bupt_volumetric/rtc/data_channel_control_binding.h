#ifndef BUPT_VOLUMETRIC_RTC_DATA_CHANNEL_CONTROL_BINDING_H_
#define BUPT_VOLUMETRIC_RTC_DATA_CHANNEL_CONTROL_BINDING_H_

#include <cstdint>

#include "api/data_channel_interface.h"
#include "bupt_volumetric/rtc/control_channel.h"

namespace bupt::volumetric {

// Binds a real (already reliable + ordered) webrtc::DataChannelInterface to a
// ControlChannel: outgoing control messages are serialized and sent as binary
// DataBuffers, and incoming DataBuffers are parsed and dispatched. This is the
// thin adapter between WebRTC's DataChannel and the WebRTC-free ControlChannel.
class DataChannelControlBinding final : public webrtc::DataChannelObserver {
 public:
  // Registers itself as the channel's observer and wires the transport.
  DataChannelControlBinding(webrtc::DataChannelInterface* channel,
                            ControlChannel* control);
  ~DataChannelControlBinding() override;

  DataChannelControlBinding(const DataChannelControlBinding&) = delete;
  DataChannelControlBinding& operator=(const DataChannelControlBinding&) = delete;

  // webrtc::DataChannelObserver.
  void OnStateChange() override;
  void OnMessage(const webrtc::DataBuffer& buffer) override;
  void OnBufferedAmountChange(std::uint64_t sent_data_size) override;

  [[nodiscard]] webrtc::DataChannelInterface::DataState state() const noexcept;

 private:
  webrtc::DataChannelInterface* channel_;
  ControlChannel* control_;
  webrtc::DataChannelInterface::DataState state_ =
      webrtc::DataChannelInterface::DataState::kConnecting;
};

}  // namespace bupt::volumetric

#endif  // BUPT_VOLUMETRIC_RTC_DATA_CHANNEL_CONTROL_BINDING_H_
