#include "bupt_volumetric/rtc/data_channel_control_binding.h"

#include <cstdint>
#include <span>
#include <vector>

#include "api/data_channel_interface.h"
#include "bupt_volumetric/rtc/control_channel.h"
#include "rtc_base/copy_on_write_buffer.h"

namespace bupt::volumetric {

DataChannelControlBinding::DataChannelControlBinding(
    webrtc::DataChannelInterface* channel,
    ControlChannel* control)
    : channel_(channel), control_(control) {
  if (control_) {
    control_->SetTransport([channel](std::span<const std::uint8_t> frame) {
      webrtc::CopyOnWriteBuffer cow(frame.data(), frame.size());
      webrtc::DataBuffer buffer(cow, /*binary=*/true);
      return channel != nullptr && channel->Send(buffer);
    });
  }
  if (channel_) {
    channel_->RegisterObserver(this);
    state_ = channel_->state();
  }
}

DataChannelControlBinding::~DataChannelControlBinding() {
  if (channel_) {
    channel_->UnregisterObserver();
  }
}

void DataChannelControlBinding::OnStateChange() {
  if (channel_) {
    state_ = channel_->state();
  }
}

void DataChannelControlBinding::OnMessage(const webrtc::DataBuffer& buffer) {
  if (control_ && buffer.binary) {
    control_->OnFrame(
        std::span<const std::uint8_t>(buffer.data.data(), buffer.data.size()));
  }
}

void DataChannelControlBinding::OnBufferedAmountChange(
    std::uint64_t /*sent_data_size*/) {}

webrtc::DataChannelInterface::DataState
DataChannelControlBinding::state() const noexcept {
  return state_;
}

}  // namespace bupt::volumetric
