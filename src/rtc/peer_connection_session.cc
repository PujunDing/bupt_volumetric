#include "bupt_volumetric/rtc/peer_connection_session.h"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/match.h"
#include "api/audio/create_audio_device_module.h"
#include "api/audio_codecs/builtin_audio_decoder_factory.h"
#include "api/audio_codecs/builtin_audio_encoder_factory.h"
#include "api/create_peerconnection_factory.h"
#include "api/environment/deprecated_global_field_trials.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_types.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/set_local_description_observer_interface.h"
#include "api/set_remote_description_observer_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "bupt_volumetric/rtc/octree_sim_decoder_factory.h"
#include "bupt_volumetric/rtc/octree_sim_encoder_factory.h"
#include "bupt_volumetric/rtc/rtc_stats_mapper.h"
#include "bupt_volumetric/rtc/semantic_payload_observer_adapter.h"
#include "bupt_volumetric/rtc/simulated_frame_source.h"
#include "bupt_volumetric/latency/latency_recorder.h"
#include "bupt_volumetric/receiver/block_assembler.h"
#include "bupt_volumetric/transport/access_unit_planner.h"
#include "bupt_volumetric/transport/simulated_access_unit.h"
#include "bupt_volumetric/transport/transport_budget.h"
#include "rtc_base/ref_counted_object.h"
#include "rtc_base/ssl_adapter.h"
#include "rtc_base/thread.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/win32_socket_init.h"
#include "video/rtp_video_stream_receiver2.h"

namespace bupt::volumetric {
namespace {

// WebRTC's native Windows embedding contract requires Winsock and the SSL
// adapter to outlive every PeerConnection. A function-local static gives the
// process one thread-safe runtime owner while still surfacing initialization
// failure through Start(), instead of relying on each executable's main().
class WebRtcRuntime final {
 public:
  WebRtcRuntime() : ssl_ready_(webrtc::InitializeSSL()) {
    // Task 12: the "OCTREE-SIM" payload maps to kVideoCodecGeneric, whose
    // PayloadTypeSupportsSkippingFecPackets() is false unless the generic
    // picture-ID trial is enabled. Without it, ShouldDisableRedAndUlpfec()
    // disables ULPFEC whenever NACK is also negotiated, silently dropping the
    // per-block repair path. The deprecated global setter is the only field
    // trial entry point linked into webrtc.lib (api/field_trials.cc is not in
    // the monolithic archive), so we use it instead of injecting `FieldTrials`.
    webrtc::DeprecatedGlobalFieldTrials::Set(
        "WebRTC-GenericPictureId/Enabled/");
  }
  ~WebRtcRuntime() {
    if (ssl_ready_) {
      webrtc::CleanupSSL();
    }
  }

  [[nodiscard]] bool ready() {
    return winsock_.error() == 0 && ssl_ready_;
  }

 private:
  webrtc::WinsockInitializer winsock_;
  const bool ssl_ready_;
};

WebRtcRuntime& Runtime() {
  static WebRtcRuntime runtime;
  return runtime;
}

std::optional<webrtc::SdpType> ToSdpType(const std::string& type) {
  if (absl::EqualsIgnoreCase(type, "offer")) {
    return webrtc::SdpType::kOffer;
  }
  if (absl::EqualsIgnoreCase(type, "answer")) {
    return webrtc::SdpType::kAnswer;
  }
  return std::nullopt;
}

struct BufferedCandidate {
  std::string mid;
  int mline = 0;
  std::string candidate;
};

struct SharedSessionState {
  explicit SharedSessionState(PeerConnectionCallbacks initial_callbacks)
      : callbacks(std::move(initial_callbacks)) {}

  bool SetState(PeerSessionState next) {
    if (stopped.load()) {
      return false;
    }
    PeerSessionState current = state.load();
    do {
      if (current == next || current == PeerSessionState::kFailed) {
        return false;
      }
    } while (!state.compare_exchange_weak(current, next));
    if (callbacks.on_state) {
      callbacks.on_state(next);
    }
    return true;
  }

  void Fail(std::string message) {
    if (SetState(PeerSessionState::kFailed) && callbacks.on_error) {
      callbacks.on_error(std::move(message));
    }
  }

  PeerConnectionCallbacks callbacks;
  std::atomic<PeerSessionState> state{PeerSessionState::kNew};
  std::atomic<bool> dtls_connected{false};
  std::atomic<bool> stopped{false};
  std::mutex mutex;
  std::deque<BufferedCandidate> buffered_candidates;
  bool remote_description_set = false;
};

bool AddCandidate(
    const webrtc::scoped_refptr<webrtc::PeerConnectionInterface>& peer,
    const BufferedCandidate& buffered,
    std::string* error_message) {
  webrtc::SdpParseError parse_error;
  std::unique_ptr<webrtc::IceCandidate> parsed = webrtc::IceCandidate::Create(
      buffered.mid, buffered.mline, buffered.candidate, &parse_error);
  if (!parsed) {
    *error_message = "failed to parse buffered ICE candidate: " +
                     parse_error.description;
    return false;
  }
  if (!peer->AddIceCandidate(parsed.get())) {
    *error_message = "failed to add buffered ICE candidate";
    return false;
  }
  return true;
}

class RemoteDescriptionObserver
    : public webrtc::SetRemoteDescriptionObserverInterface {
 public:
  RemoteDescriptionObserver(
      webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer,
      std::shared_ptr<SharedSessionState> shared)
      : peer_(std::move(peer)), shared_(std::move(shared)) {}

  void OnSetRemoteDescriptionComplete(webrtc::RTCError error) override {
    if (!error.ok()) {
      shared_->Fail("failed to set remote description: " +
                    std::string(error.message()));
      return;
    }
    std::deque<BufferedCandidate> pending;
    {
      std::lock_guard<std::mutex> lock(shared_->mutex);
      shared_->remote_description_set = true;
      pending.swap(shared_->buffered_candidates);
    }
    for (const BufferedCandidate& candidate : pending) {
      std::string add_error;
      if (!AddCandidate(peer_, candidate, &add_error)) {
        shared_->Fail(std::move(add_error));
        return;
      }
    }
  }

 private:
  const webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  const std::shared_ptr<SharedSessionState> shared_;
};

class LocalDescriptionObserver
    : public webrtc::SetLocalDescriptionObserverInterface {
 public:
  LocalDescriptionObserver(std::shared_ptr<SharedSessionState> shared,
                           std::string type,
                           std::string sdp)
      : shared_(std::move(shared)),
        type_(std::move(type)),
        sdp_(std::move(sdp)) {}

  void OnSetLocalDescriptionComplete(webrtc::RTCError error) override {
    if (!error.ok()) {
      shared_->Fail("failed to set local description: " +
                    std::string(error.message()));
      return;
    }
    if (!shared_->stopped.load() && shared_->callbacks.on_local_description) {
      shared_->callbacks.on_local_description(std::move(type_),
                                              std::move(sdp_));
    }
  }

 private:
  const std::shared_ptr<SharedSessionState> shared_;
  std::string type_;
  std::string sdp_;
};

class CreateDescriptionObserver
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  CreateDescriptionObserver(
      webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer,
      std::shared_ptr<SharedSessionState> shared)
      : peer_(std::move(peer)), shared_(std::move(shared)) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* raw_desc) override {
    std::unique_ptr<webrtc::SessionDescriptionInterface> desc(raw_desc);
    std::string sdp;
    if (!desc->ToString(&sdp)) {
      shared_->Fail("failed to serialize local description");
      return;
    }
    const std::string type = desc->type();
    auto observer = webrtc::scoped_refptr<LocalDescriptionObserver>(
        new webrtc::RefCountedObject<LocalDescriptionObserver>(shared_, type,
                                                               sdp));
    peer_->SetLocalDescription(std::move(desc), std::move(observer));
  }

  void OnFailure(webrtc::RTCError error) override {
    shared_->Fail("failed to create local description: " +
                  std::string(error.message()));
  }

 private:
  const webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_;
  const std::shared_ptr<SharedSessionState> shared_;
};

// One-shot asynchronous stats callback. WebRTC holds a scoped_refptr until
// OnStatsDelivered fires, so the shared store/state stay alive for the lifetime
// of the request; no hidden polling thread or blocking wait is introduced.
class StatsCallback : public webrtc::RTCStatsCollectorCallback {
 public:
  StatsCallback(std::shared_ptr<TransportFeedbackStore> store,
                std::shared_ptr<SharedSessionState> shared)
      : store_(std::move(store)), shared_(std::move(shared)) {}

  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override {
    if (!report || shared_->stopped.load()) {
      return;
    }
    store_->OnStats(MapRtcStats(*report));
  }

 private:
  const std::shared_ptr<TransportFeedbackStore> store_;
  const std::shared_ptr<SharedSessionState> shared_;
};

}  // namespace

class PeerConnectionSessionImpl final
    : public PeerConnectionSession,
      public webrtc::PeerConnectionObserver {
 public:
  explicit PeerConnectionSessionImpl(PeerConnectionCallbacks callbacks)
      : shared_(std::make_shared<SharedSessionState>(std::move(callbacks))),
        feedback_store_(std::make_shared<TransportFeedbackStore>()),
        completed_frames_(std::make_shared<std::atomic<std::uint64_t>>(0)),
        completed_blocks_(std::make_shared<std::atomic<std::uint64_t>>(0)),
        latency_samples_(std::make_shared<LatencySampleQueue>()) {}
  ~PeerConnectionSessionImpl() override { Stop(); }

  SessionStatus Start(const RtcSessionConfig& config) override;
  SessionStatus CreateOffer() override;
  SessionStatus CreateAnswer() override;
  SessionStatus ApplyRemoteDescription(std::string type,
                                       std::string sdp) override;
  SessionStatus AddRemoteIceCandidate(std::string mid,
                                      int mline,
                                      std::string candidate) override;
  SessionStatus SubmitSimulatedFrame(
      SimulatedOctreeFrame frame, std::uint32_t rtp_timestamp) override;
  SessionStatus CollectTransportStats() override;
  SessionStatus Stop() noexcept override;
  PeerSessionState state() const noexcept override {
    return shared_->state.load();
  }
  bool dtls_connected() const noexcept override {
    return shared_->dtls_connected.load();
  }
  TransportFeedbackSnapshot transport_feedback() const override {
    return feedback_store_->Snapshot();
  }
  std::uint64_t completed_frame_count() const noexcept override {
    return completed_frames_->load(std::memory_order_relaxed);
  }
  std::uint64_t completed_block_count() const noexcept override {
    return completed_blocks_->load(std::memory_order_relaxed);
  }
  std::vector<std::uint64_t> DrainLatencyUs() override {
    return latency_samples_ ? latency_samples_->Drain()
                            : std::vector<std::uint64_t>{};
  }

  // PeerConnectionObserver.
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override {}
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>)
      override {}
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override {}
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
  void OnIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState new_state) override;
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState new_state) override;
  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>) override {}

 private:
  void SetState(PeerSessionState next) { (void)shared_->SetState(next); }

  const std::shared_ptr<SharedSessionState> shared_;
  const std::shared_ptr<TransportFeedbackStore> feedback_store_;
  const std::shared_ptr<std::atomic<std::uint64_t>> completed_frames_;
  const std::shared_ptr<std::atomic<std::uint64_t>> completed_blocks_;
  const std::shared_ptr<LatencySampleQueue> latency_samples_;

  std::unique_ptr<webrtc::Thread> network_thread_;
  std::unique_ptr<webrtc::Thread> worker_thread_;
  std::unique_ptr<webrtc::Thread> signaling_thread_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> peer_connection_;
  webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver_;
  webrtc::scoped_refptr<SimulatedFrameSource> simulated_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  std::shared_ptr<SimulatedAccessUnitQueue> access_units_;
  std::shared_ptr<BlockAssembler> block_assembler_;
  std::unique_ptr<SemanticPayloadObserverAdapter> observer_adapter_;

  MediaDirection direction_ = MediaDirection::kSendOnly;
  std::uint64_t bootstrap_bitrate_bps_ = 0;
};

SessionStatus PeerConnectionSessionImpl::Start(const RtcSessionConfig& config) {
  if (shared_->state.load() != PeerSessionState::kNew) {
    return SessionStatus::Error("session already started");
  }
  if (!shared_->callbacks.on_local_description ||
      !shared_->callbacks.on_local_ice_candidate ||
      !shared_->callbacks.on_state) {
    return SessionStatus::Error("all session callbacks are required");
  }
  const std::string validation = ValidateRtcSessionConfig(config);
  if (!validation.empty()) {
    return SessionStatus::Error(validation);
  }
  if (!Runtime().ready()) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to initialize Winsock/SSL runtime");
  }
  direction_ = config.direction;
  bootstrap_bitrate_bps_ = config.bootstrap_bitrate_bps;
  SetState(PeerSessionState::kStarting);

  network_thread_ = webrtc::Thread::CreateWithSocketServer();
  worker_thread_ = webrtc::Thread::Create();
  signaling_thread_ = webrtc::Thread::Create();
  if (!network_thread_ || !worker_thread_ || !signaling_thread_) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to allocate WebRTC threads");
  }
  if (!network_thread_->Start() || !worker_thread_->Start() ||
      !signaling_thread_->Start()) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to start WebRTC threads");
  }

  const webrtc::scoped_refptr<webrtc::AudioDeviceModule> audio_device =
      webrtc::CreateAudioDeviceModule(webrtc::CreateEnvironment(),
                                      webrtc::AudioDeviceModule::kDummyAudio);
  access_units_ = std::make_shared<SimulatedAccessUnitQueue>(2);
  if (direction_ == MediaDirection::kReceiveOnly) {
    // Task 13: wire the post-FEC source-payload observer to a semantic block
    // assembler. The global registration is read when WebRTC constructs the
    // receive stream (during offer/answer), which happens after Start(), so
    // registering here is early enough. Stop() clears it.
    block_assembler_ = std::make_shared<BlockAssembler>(
        /*max_inflight_blocks=*/8, /*max_buffered_bytes=*/4'000'000);
    observer_adapter_ = std::make_unique<SemanticPayloadObserverAdapter>(
        block_assembler_, completed_blocks_);
    webrtc::SetGlobalPostFecSourcePayloadObserver(observer_adapter_.get());
  }
  factory_ = webrtc::CreatePeerConnectionFactory(
      network_thread_.get(), worker_thread_.get(), signaling_thread_.get(),
      audio_device, webrtc::CreateBuiltinAudioEncoderFactory(),
      webrtc::CreateBuiltinAudioDecoderFactory(),
      std::make_unique<OctreeSimEncoderFactory>(access_units_,
                                                 feedback_store_),
      std::make_unique<OctreeSimDecoderFactory>(completed_frames_,
                                                 latency_samples_), nullptr,
      nullptr);
  if (!factory_) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to create PeerConnectionFactory");
  }

  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config =
      BuildRtcConfiguration(config);
  webrtc::PeerConnectionDependencies dependencies(this);
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::PeerConnectionInterface>>
      pc_result = factory_->CreatePeerConnectionOrError(
          rtc_config, std::move(dependencies));
  if (!pc_result.ok()) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error(std::string("failed to create PeerConnection: ") +
                                pc_result.error().message());
  }
  peer_connection_ = pc_result.MoveValue();

  // Add one video transceiver so ICE and DTLS have a media section to
  // negotiate, in the direction of this endpoint.
  webrtc::RTCErrorOr<webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>>
      transceiver = peer_connection_->AddTransceiver(webrtc::MediaType::VIDEO);
  if (!transceiver.ok()) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to add video transceiver");
  }
  transceiver_ = transceiver.MoveValue();
  const webrtc::RTCError direction_error = transceiver_->SetDirectionWithError(
      direction_ == MediaDirection::kSendOnly
          ? webrtc::RtpTransceiverDirection::kSendOnly
          : webrtc::RtpTransceiverDirection::kRecvOnly);
  if (!direction_error.ok()) {
    SetState(PeerSessionState::kFailed);
    return SessionStatus::Error("failed to set transceiver direction: " +
                                std::string(direction_error.message()));
  }

  if (direction_ == MediaDirection::kSendOnly) {
    simulated_source_ =
        webrtc::make_ref_counted<SimulatedFrameSource>(access_units_);
    video_track_ =
        factory_->CreateVideoTrack(simulated_source_, "octree-sim");
    if (!video_track_ ||
        !transceiver_->sender()->SetTrack(video_track_.get())) {
      SetState(PeerSessionState::kFailed);
      return SessionStatus::Error("failed to attach simulated video track");
    }
  }

  SetState(PeerSessionState::kConnecting);
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::CreateOffer() {
  if (!peer_connection_ ||
      shared_->state.load() != PeerSessionState::kConnecting) {
    return SessionStatus::Error("session is not ready to create an offer");
  }
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  auto observer = webrtc::scoped_refptr<CreateDescriptionObserver>(
      new webrtc::RefCountedObject<CreateDescriptionObserver>(peer_connection_,
                                                              shared_));
  peer_connection_->CreateOffer(observer.get(), options);
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::CreateAnswer() {
  if (!peer_connection_ ||
      shared_->state.load() != PeerSessionState::kConnecting) {
    return SessionStatus::Error("session is not ready to create an answer");
  }
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
  auto observer = webrtc::scoped_refptr<CreateDescriptionObserver>(
      new webrtc::RefCountedObject<CreateDescriptionObserver>(peer_connection_,
                                                              shared_));
  peer_connection_->CreateAnswer(observer.get(), options);
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::ApplyRemoteDescription(std::string type,
                                                                std::string sdp) {
  if (!peer_connection_) {
    return SessionStatus::Error("session is not started");
  }
  const std::optional<webrtc::SdpType> sdp_type = ToSdpType(type);
  if (!sdp_type) {
    return SessionStatus::Error("remote description type must be offer/answer");
  }
  std::unique_ptr<webrtc::SessionDescriptionInterface> desc =
      webrtc::CreateSessionDescription(*sdp_type, std::move(sdp));
  if (!desc) {
    return SessionStatus::Error("failed to parse remote SDP");
  }
  webrtc::scoped_refptr<webrtc::SetRemoteDescriptionObserverInterface>
      remote_observer(new webrtc::RefCountedObject<RemoteDescriptionObserver>(
          peer_connection_, shared_));
  peer_connection_->SetRemoteDescription(std::move(desc),
                                         std::move(remote_observer));
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::AddRemoteIceCandidate(
    std::string mid,
    int mline,
    std::string candidate) {
  if (!peer_connection_) {
    return SessionStatus::Error("session is not started");
  }
  {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (!shared_->remote_description_set) {
      shared_->buffered_candidates.push_back(
          {std::move(mid), mline, std::move(candidate)});
      return SessionStatus::Ok();
    }
  }
  webrtc::SdpParseError error;
  std::unique_ptr<webrtc::IceCandidate> parsed =
      webrtc::IceCandidate::Create(mid, mline, candidate, &error);
  if (!parsed) {
    return SessionStatus::Error("failed to parse ICE candidate");
  }
  if (!peer_connection_->AddIceCandidate(parsed.get())) {
    return SessionStatus::Error("failed to add ICE candidate");
  }
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::SubmitSimulatedFrame(
    SimulatedOctreeFrame frame, std::uint32_t rtp_timestamp) {
  if (direction_ != MediaDirection::kSendOnly) {
    return SessionStatus::Error(
        "simulated frames require a send-only session");
  }
  if (!simulated_source_ || shared_->stopped.load()) {
    return SessionStatus::Error("send-only session is not running");
  }
  if (frame.payload().size() !=
      kMaxSimulatedAccessUnitPayloadBytes) {
    return SessionStatus::Error(
        "simulated frame must contain exactly 2000000 bytes");
  }
  // Admit P0/P1/P2 blocks against the latest rate budget and queue one access
  // unit per admitted block, so each block becomes its own FEC unit. Before the
  // first WebRTC SetRates lands, use a bootstrap budget so the first frames can
  // flow and kick-start the bandwidth estimator.
  const std::uint64_t bootstrap_bitrate =
      bootstrap_bitrate_bps_ == 0 ? 1'000'000 : bootstrap_bitrate_bps_;
  const TransportFeedbackSnapshot feedback = feedback_store_->Snapshot();
  const std::uint64_t target_bitrate =
      feedback.target_bitrate_bps == 0 ? bootstrap_bitrate
                                       : feedback.target_bitrate_bps;
  const WindowBudget budget = TransportBudgetCalculator().BuildWindowBudget(
      target_bitrate, /*window_ms=*/2'000, /*reserve=*/0);
  const std::vector<SimulatedAccessUnit> units =
      PlanAdmittedAccessUnits(frame, rtp_timestamp,
                              budget.available_media_bytes);
  const std::uint64_t capture_us = webrtc::TimeMicros();
  for (SimulatedAccessUnit unit : units) {
    unit.capture_time_us = capture_us;
    simulated_source_->Push(std::move(unit));
  }
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::CollectTransportStats() {
  if (!peer_connection_ || shared_->stopped.load()) {
    return SessionStatus::Error("session is not running");
  }
  webrtc::scoped_refptr<StatsCallback> callback =
      webrtc::make_ref_counted<StatsCallback>(feedback_store_, shared_);
  peer_connection_->GetStats(callback.get());
  return SessionStatus::Ok();
}

SessionStatus PeerConnectionSessionImpl::Stop() noexcept {
  const PeerSessionState current = shared_->state.load();
  if (current == PeerSessionState::kStopped) {
    return SessionStatus::Ok();
  }
  shared_->stopped.store(true);
  shared_->state.store(PeerSessionState::kStopped);
  if (shared_->callbacks.on_state) {
    shared_->callbacks.on_state(PeerSessionState::kStopped);
  }
  if (peer_connection_) {
    peer_connection_->Close();
  }
  // Drop every WebRTC proxy reference while the signaling thread is still
  // running, so each proxy destructor can marshal its internal teardown.
  if (transceiver_ && transceiver_->sender()) {
    (void)transceiver_->sender()->SetTrack(nullptr);
  }
  video_track_ = nullptr;
  simulated_source_ = nullptr;
  transceiver_ = nullptr;
  peer_connection_ = nullptr;
  factory_ = nullptr;
  access_units_.reset();
  // Clear the Task 13 global observer after the PeerConnection (and therefore
  // the receive stream that referenced the adapter) is gone, then release the
  // adapter before it could be read by any future session.
  webrtc::SetGlobalPostFecSourcePayloadObserver(nullptr);
  observer_adapter_.reset();
  block_assembler_.reset();
  if (signaling_thread_) {
    signaling_thread_->Stop();
  }
  if (worker_thread_) {
    worker_thread_->Stop();
  }
  if (network_thread_) {
    network_thread_->Stop();
  }
  return SessionStatus::Ok();
}

void PeerConnectionSessionImpl::OnIceCandidate(
    const webrtc::IceCandidate* candidate) {
  if (candidate && !shared_->stopped.load() &&
      shared_->callbacks.on_local_ice_candidate) {
    shared_->callbacks.on_local_ice_candidate(candidate->sdp_mid(),
                                              candidate->sdp_mline_index(),
                                              candidate->ToString());
  }
}

void PeerConnectionSessionImpl::OnIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState new_state) {
  if (new_state == webrtc::PeerConnectionInterface::kIceConnectionConnected ||
      new_state == webrtc::PeerConnectionInterface::kIceConnectionCompleted) {
    // ICE alone is not an application-level connection: wait for the
    // standardized PeerConnection state, which also includes DTLS.
  } else if (new_state ==
             webrtc::PeerConnectionInterface::kIceConnectionFailed) {
    shared_->Fail("ICE connection failed");
  }
}

void PeerConnectionSessionImpl::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState new_state) {
  // kConnected is reported only after both ICE and the DTLS handshake complete.
  const bool connected =
      new_state ==
      webrtc::PeerConnectionInterface::PeerConnectionState::kConnected;
  shared_->dtls_connected.store(connected);
  if (connected) {
    SetState(PeerSessionState::kConnected);
  } else if (new_state ==
             webrtc::PeerConnectionInterface::PeerConnectionState::kFailed) {
    shared_->Fail("PeerConnection failed");
  }
}

std::unique_ptr<PeerConnectionSession> CreatePeerConnectionSession(
    PeerConnectionCallbacks callbacks) {
  return std::make_unique<PeerConnectionSessionImpl>(std::move(callbacks));
}

}  // namespace bupt::volumetric
