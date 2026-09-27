# Task 11 WebRTC Congestion Control Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a no-UI, simulated-volumetric media path whose bytes traverse real WebRTC RTP/pacer/SRTP while TWCC/GoogCC rate updates drive semantic admission and RTCStats expose auditable transport observations.

**Architecture:** Domain-owned feedback, bitrate sampling, simulated-access-unit, and admission types remain independent of WebRTC. Thin RTC adapters translate codec callbacks and `RTCStatsReport` objects into those types. One narrowly documented vendored patch exports WebRTC's existing pacer delay through candidate-pair stats without modifying congestion-control behavior.

**Tech Stack:** C++20, native WebRTC commit `8a3d684776938a4614f892f81859136ff4855ea3`, GoogleTest, CMake/Ninja, Windows host/UDP ICE, DTLS/SRTP, TWCC/GoogCC.

---

## Execution rules

- Work from `D:\BUPT-XR`; the workspace root and `bupt_volumetric` are not Git repositories, so each task ends with a documented verification checkpoint instead of a commit.
- Preserve all pre-existing edits under `extern/webrtc/src`; never run `git reset`, `git clean`, `gclient sync`, or regenerate `out/Default`.
- Use `apply_patch` for source edits.
- For every production behavior: add one focused failing test, run it and record the expected failure, add only enough production code to pass, rerun the focused test, then rerun the nearest suite.
- Build the narrowest WebRTC target before rebuilding `webrtc.lib`.
- Do not mark Task 11 complete while any required test is skipped.

## File map

### Domain and application policy

- Create `bupt_volumetric/include/bupt_volumetric/transport/transport_feedback.h`: WebRTC-free rate update, observation snapshot, thread-safe store, and actual-bitrate sampler contracts.
- Implement `bupt_volumetric/src/transport/transport_feedback.cc`.
- Create `bupt_volumetric/include/bupt_volumetric/transport/simulated_access_unit.h`: OCTREE-SIM access-unit envelope and bounded queue contracts.
- Implement `bupt_volumetric/src/transport/simulated_access_unit.cc`.
- Modify `bupt_volumetric/include/bupt_volumetric/metrics/metric_event.h`: congestion metric schema with observed/derived/estimated provenance.
- Modify `bupt_volumetric/CMakeLists.txt`: compile new model files and register congestion/metrics tests.

### WebRTC adapters

- Modify `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder.h` and `src/rtc/octree_sim_encoder.cc`: consume queued access units, emit `EncodedImage`, forward `SetRates` into the domain sink.
- Modify both OCTREE-SIM factory headers/sources: inject the shared queue, feedback store, and receiver observer.
- Modify `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_decoder.h` and `src/rtc/octree_sim_decoder.cc`: validate received access units and issue a no-UI completion callback.
- Create `bupt_volumetric/include/bupt_volumetric/rtc/simulated_frame_source.h` and `src/rtc/simulated_frame_source.cc`: push a tiny I420 scheduling frame only after queuing matching semantic bytes.
- Create `bupt_volumetric/include/bupt_volumetric/rtc/rtc_stats_mapper.h` and `src/rtc/rtc_stats_mapper.cc`: convert WebRTC stats into domain observations.
- Modify `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h` and `src/rtc/peer_connection_session.cc`: attach send track, expose simulated-frame submission and asynchronous stats collection, and preserve ordered shutdown.

### Minimal vendored WebRTC observation patch

- Modify `extern/webrtc/src/api/stats/rtcstats_objects.h`.
- Modify `extern/webrtc/src/stats/rtcstats_objects.cc`.
- Modify `extern/webrtc/src/pc/rtc_stats_collector.cc`.
- Modify `extern/webrtc/src/pc/rtc_stats_collector_unittest.cc`.
- Modify `bupt_volumetric/webrtc_patches/manifest.json` and `bupt_volumetric/webrtc_patches/README.md`.

### Tests

- Create `test/test_volumetric_congestion/transport_feedback_test.cc`.
- Create `test/test_volumetric_congestion/webrtc_rate_callback_test.cc`.
- Create `test/test_volumetric_rtc/simulated_access_unit_test.cc`.
- Create `test/test_volumetric_rtc/octree_sim_media_test.cc`.
- Create `test/test_volumetric_rtc/twcc_negotiation_test.cc`.
- Create `test/test_volumetric_metrics/rtc_stats_mapping_test.cc`.
- Create `test/test_volumetric_rtc/localhost_media_flow_test.cc`.
- Create `test/test_volumetric_congestion/capacity_step_test.cc`.
- Modify the relevant test `CMakeLists.txt` files.

## Task 1: Domain feedback store and bitrate sampler

**Files:**

- Replace: `bupt_volumetric/include/bupt_volumetric/transport/transport_feedback.h`
- Replace: `bupt_volumetric/src/transport/transport_feedback.cc`
- Modify: `bupt_volumetric/CMakeLists.txt`
- Modify: `test/test_volumetric_congestion/CMakeLists.txt`
- Create: `test/test_volumetric_congestion/transport_feedback_test.cc`

- [x] **Step 1: Add focused failing tests**

Define tests through the public API:

```cpp
TEST(TransportFeedbackStore, MissingObservationsRemainUnavailable) {
  TransportFeedbackStore store;
  store.OnRateUpdate(EncoderRateUpdate{2'000'000, 30.0, 100});
  const TransportFeedbackSnapshot snapshot = store.Snapshot();
  EXPECT_EQ(snapshot.target_bitrate_bps, 2'000'000u);
  EXPECT_EQ(snapshot.target_framerate_fps, 30.0);
  EXPECT_FALSE(snapshot.selected_pair_rtt_ms.has_value());
  EXPECT_FALSE(snapshot.loss_fraction.has_value());
  EXPECT_FALSE(snapshot.pacer_delay_ms.has_value());
}

TEST(ActualBitrateSampler, DerivesRateOnlyFromValidCounterDelta) {
  ActualBitrateSampler sampler;
  EXPECT_FALSE(sampler.Sample(1'000, 10'000).has_value());
  EXPECT_EQ(sampler.Sample(1'500, 11'000), 4'000u);
  EXPECT_FALSE(sampler.Sample(1'400, 12'000).has_value());
  EXPECT_FALSE(sampler.Sample(1'600, 12'000).has_value());
}
```

- [x] **Step 2: Register and run RED**

Add:

```cmake
bupt_volumetric_add_test(volumetric_transport_feedback_test
                         transport_feedback_test.cc)
```

Run:

```powershell
cmake --build --preset windows-clang-release --target volumetric_transport_feedback_test
```

Expected: compile failure because `EncoderRateUpdate`, `TransportFeedbackStore`, and `ActualBitrateSampler` do not exist.

- [x] **Step 3: Add the WebRTC-free public contract**

Use this shape:

```cpp
struct EncoderRateUpdate final {
  std::uint64_t target_bitrate_bps = 0;
  double target_framerate_fps = 0.0;
  std::uint64_t observed_at_ms = 0;
};

struct RtcTransportObservation final {
  std::uint64_t observed_at_ms = 0;
  std::optional<double> selected_pair_rtt_ms;
  std::optional<std::uint64_t> available_outgoing_bitrate_bps;
  std::optional<std::uint64_t> outbound_bytes;
  std::optional<std::uint64_t> outbound_packets;
  std::optional<std::uint64_t> retransmitted_bytes;
  std::optional<std::uint64_t> retransmitted_packets;
  std::optional<double> total_packet_send_delay_ms;
  std::optional<double> loss_fraction;
  std::optional<double> jitter_ms;
  std::optional<double> remote_round_trip_time_ms;
  std::optional<double> pacer_delay_ms;
};

struct TransportFeedbackSnapshot final {
  std::uint64_t target_bitrate_bps = 0;
  double target_framerate_fps = 0.0;
  std::optional<double> selected_pair_rtt_ms;
  std::optional<std::uint64_t> available_outgoing_bitrate_bps;
  std::optional<std::uint64_t> outbound_bytes;
  std::optional<std::uint64_t> outbound_packets;
  std::optional<std::uint64_t> retransmitted_bytes;
  std::optional<std::uint64_t> retransmitted_packets;
  std::optional<double> total_packet_send_delay_ms;
  std::optional<double> loss_fraction;
  std::optional<double> jitter_ms;
  std::optional<double> remote_round_trip_time_ms;
  std::optional<double> pacer_delay_ms;
  std::optional<std::uint64_t> actual_sent_bitrate_bps;
};

class RateUpdateSink {
 public:
  virtual ~RateUpdateSink() = default;
  virtual void OnRateUpdate(const EncoderRateUpdate& update) = 0;
};

class TransportFeedbackStore final : public RateUpdateSink {
 public:
  void OnRateUpdate(const EncoderRateUpdate& update) override;
  void OnStats(const RtcTransportObservation& observation);
  [[nodiscard]] TransportFeedbackSnapshot Snapshot() const;
 private:
  mutable std::mutex mutex_;
  TransportFeedbackSnapshot snapshot_;
  ActualBitrateSampler bitrate_sampler_;
};
```

`ActualBitrateSampler::Sample(bytes, observed_at_ms)` stores the first sample, computes `(delta_bytes * 8000) / delta_ms`, rejects counter regression and non-increasing time, and resets its baseline after either rejection.

- [x] **Step 4: Run GREEN and nearest regression suite**

```powershell
cmake --build --preset windows-clang-release --target volumetric_transport_feedback_test
ctest --preset windows-clang-release -R "TransportFeedback|TransportBudget|SemanticAdmission" --output-on-failure
```

Expected: all selected tests pass with zero skips.

- [x] **Step 5: Record checkpoint**

Update `docs/progress.md` with the RED reason, GREEN commands, test count, and note that no WebRTC code changed.

## Task 2: Deterministic simulated access-unit envelope and bounded queue

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/transport/simulated_access_unit.h`
- Create: `bupt_volumetric/src/transport/simulated_access_unit.cc`
- Create: `test/test_volumetric_rtc/simulated_access_unit_test.cc`
- Modify: `bupt_volumetric/CMakeLists.txt`
- Modify: `test/test_volumetric_rtc/CMakeLists.txt`

- [x] **Step 1: Write RED tests for round trip, corruption, and queue eviction**

```cpp
TEST(SimulatedAccessUnit, RoundTripsIdentityAndPayload) {
  const SimulatedAccessUnit unit = BuildUnit(7, 90'000, {1, 2, 3, 4});
  const std::vector<std::uint8_t> wire = SerializeSimulatedAccessUnit(unit);
  const auto decoded = ParseSimulatedAccessUnit(wire);
  ASSERT_TRUE(decoded.ok());
  EXPECT_EQ(decoded.value().frame_id, 7u);
  EXPECT_EQ(decoded.value().rtp_timestamp, 90'000u);
  EXPECT_EQ(decoded.value().payload,
            (std::vector<std::uint8_t>{1, 2, 3, 4}));
}

TEST(SimulatedAccessUnit, RejectsChecksumCorruption) {
  std::vector<std::uint8_t> wire =
      SerializeSimulatedAccessUnit(BuildUnit(7, 90'000, {1, 2, 3, 4}));
  wire.back() ^= 0xff;
  EXPECT_EQ(ParseSimulatedAccessUnit(wire).error(),
            SimulatedAccessUnitError::kChecksumMismatch);
}

TEST(SimulatedAccessUnitQueue, EvictsOldestAndMatchesTimestamp) {
  SimulatedAccessUnitQueue queue(2);
  queue.Push(BuildUnit(1, 1'000, {1}));
  queue.Push(BuildUnit(2, 2'000, {2}));
  queue.Push(BuildUnit(3, 3'000, {3}));
  EXPECT_EQ(queue.evicted_count(), 1u);
  EXPECT_FALSE(queue.Pop(1'000).has_value());
  EXPECT_EQ(queue.Pop(3'000)->frame_id, 3u);
}
```

- [x] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_simulated_access_unit_test
```

Expected: compile failure for the missing envelope and queue types.

- [x] **Step 3: Implement the envelope**

Use a fixed network-order header:

```cpp
struct SimulatedAccessUnit final {
  std::uint64_t frame_id = 0;
  std::uint32_t rtp_timestamp = 0;
  std::vector<std::uint8_t> payload;
};

inline constexpr std::uint32_t kOctreeSimMagic = 0x4f435453;  // OCTS
inline constexpr std::uint8_t kOctreeSimVersion = 1;
```

The serialized fields are magic, version, reserved bytes, frame_id, RTP timestamp, payload length, FNV-1a-32 checksum, then payload. Parsing rejects bad magic/version, truncated data, declared-length mismatch, payloads above 2,000,000 bytes, and checksum mismatch. `SimulatedAccessUnitQueue` uses a mutex, `std::deque`, capacity >= 1, timestamp lookup/removal, and oldest-first eviction.

- [x] **Step 4: Run GREEN and corruption cases**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_simulated_access_unit_test
ctest --preset windows-clang-runtime -R SimulatedAccessUnit --output-on-failure
```

Expected: all envelope and queue tests pass.

- [x] **Step 5: Record checkpoint**

Record wire version, maximum length, checksum algorithm, and test results in `docs/progress.md`.

## Task 3: Encoder rate callback drives semantic admission

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_encoder.cc`
- Replace: `test/test_volumetric_rtc/octree_sim_encoder_rate_test.cc`
- Create: `test/test_volumetric_congestion/webrtc_rate_callback_test.cc`
- Modify: `test/test_volumetric_congestion/CMakeLists.txt`

- [x] **Step 1: Write RED tests against the new sink signature**

```cpp
TEST(WebRtcRateCallback, LatestEncoderRateChangesAvailableFrameBudget) {
  auto feedback = std::make_shared<TransportFeedbackStore>();
  OctreeSimEncoder encoder(nullptr, feedback);
  encoder.SetRates(Rates(8'000'000, 30.0));
  const WindowBudget high = TransportBudgetCalculator().BuildWindowBudget(
      feedback->Snapshot().target_bitrate_bps, 33, 0);
  encoder.SetRates(Rates(800'000, 30.0));
  const WindowBudget low = TransportBudgetCalculator().BuildWindowBudget(
      feedback->Snapshot().target_bitrate_bps, 33, 0);
  EXPECT_GT(high.available_media_bytes, low.available_media_bytes);
  EXPECT_EQ(low.available_media_bytes, 3'300u);
}
```

- [x] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_webrtc_rate_callback_test
```

Expected: compile failure because `RateUpdateSink` still uses two scalar arguments and the encoder constructor lacks the queue/store form.

- [x] **Step 3: Move the sink contract inward and update SetRates**

Remove the RTC-local `RateUpdateSink` declaration. Include the domain header and publish:

```cpp
rate_sink_->OnRateUpdate(EncoderRateUpdate{
    .target_bitrate_bps = parameters.target_bitrate.get_sum_bps(),
    .target_framerate_fps = parameters.framerate_fps,
    .observed_at_ms = static_cast<std::uint64_t>(
        webrtc::TimeMillis()),
});
```

Keep the existing target snapshot accessors for diagnostics. Update existing recording sinks to receive `const EncoderRateUpdate&` and verify concurrent callbacks still arrive exactly once.

- [x] **Step 4: Run GREEN and rate regressions**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_webrtc_rate_callback_test volumetric_octree_sim_encoder_rate_test
ctest --preset windows-clang-runtime -R "WebRtcRateCallback|OctreeSimEncoderRate" --output-on-failure
```

Expected: both targets pass.

- [x] **Step 5: Record checkpoint**

Document that `SetRates` is now the sole high-frequency capacity authority; RTCStats remains observational.

## Task 4: OCTREE-SIM emits and consumes real encoded access units

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_encoder.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_decoder.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_decoder.cc`
- Modify: both codec factory headers and sources
- Create: `test/test_volumetric_rtc/octree_sim_media_test.cc`
- Modify: `test/test_volumetric_rtc/CMakeLists.txt`

- [ ] **Step 1: Write RED tests for encoder emission and decoder validation**

Use real WebRTC callbacks, not mocks of serialization:

```cpp
TEST(OctreeSimMedia, EncoderEmitsQueuedUnitForMatchingRtpTimestamp) {
  auto queue = std::make_shared<SimulatedAccessUnitQueue>(2);
  queue->Push(BuildUnit(42, 90'000, {1, 2, 3, 4}));
  RecordingEncodedCallback callback;
  OctreeSimEncoder encoder(queue, nullptr);
  ASSERT_EQ(encoder.InitEncode(&Codec(), Settings()), WEBRTC_VIDEO_CODEC_OK);
  encoder.RegisterEncodeCompleteCallback(&callback);
  EXPECT_EQ(encoder.Encode(FrameWithTimestamp(90'000), nullptr),
            WEBRTC_VIDEO_CODEC_OK);
  ASSERT_EQ(callback.images.size(), 1u);
  EXPECT_EQ(Parse(callback.images[0]).frame_id, 42u);
}

TEST(OctreeSimMedia, DecoderRejectsCorruptEnvelopeWithoutCompletion) {
  RecordingDecodedCallback callback;
  OctreeSimDecoder decoder;
  decoder.RegisterDecodeCompleteCallback(&callback);
  EXPECT_EQ(decoder.Decode(CorruptEncodedImage(), 0),
            WEBRTC_VIDEO_CODEC_ERROR);
  EXPECT_EQ(callback.decoded_count, 0u);
  EXPECT_EQ(decoder.validation_failure_count(), 1u);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_octree_sim_media_test
```

Expected: compile failure for missing queue constructor, encoded callback behavior, and decoder counters.

- [ ] **Step 3: Implement minimum real codec behavior**

Encoder behavior:

```cpp
const auto unit = access_units_->Pop(frame.rtp_timestamp());
if (!unit) return WEBRTC_VIDEO_CODEC_NO_OUTPUT;
const std::vector<std::uint8_t> wire = SerializeSimulatedAccessUnit(*unit);
webrtc::EncodedImage image;
image.SetEncodedData(webrtc::EncodedImageBuffer::Create(wire.data(), wire.size()));
image.SetRtpTimestamp(frame.rtp_timestamp());
image._frameType = webrtc::VideoFrameType::kVideoFrameKey;
image._encodedWidth = 2;
image._encodedHeight = 2;
return callback_->OnEncodedImage(image, nullptr).error ==
               webrtc::EncodedImageCallback::Result::Error::OK
           ? WEBRTC_VIDEO_CODEC_OK
           : WEBRTC_VIDEO_CODEC_ERROR;
```

Decoder copies the encoded bytes, parses and validates the envelope, increments received/completed/failure counters under a mutex, and invokes `DecodedImageCallback::Decoded()` with a 2x2 black I420 frame carrying the RTP timestamp. No renderer or semantic reconstruction is created.

- [ ] **Step 4: Run GREEN and factory regression tests**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_octree_sim_media_test volumetric_octree_sim_codec_factory_test
ctest --preset windows-clang-runtime -R "OctreeSimMedia|OctreeSimCodec" --output-on-failure
```

Expected: tests pass and encoder callback contains a non-empty buffer.

- [ ] **Step 5: Record checkpoint**

Document that codec work is serialization/validation only, not point-cloud compression or rendering.

## Task 5: Attach a no-UI scheduling source and prove TWCC negotiation

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/rtc/simulated_frame_source.h`
- Create: `bupt_volumetric/src/rtc/simulated_frame_source.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h`
- Modify: `bupt_volumetric/src/rtc/peer_connection_session.cc`
- Create: `test/test_volumetric_rtc/twcc_negotiation_test.cc`
- Modify: `test/test_volumetric_rtc/CMakeLists.txt`

- [ ] **Step 1: Write RED SDP and source-submission tests**

```cpp
TEST(TwccNegotiation, OfferContainsTransportWideExtensionAndFeedback) {
  const std::string offer = CreateOfferFromStartedSendSession();
  EXPECT_NE(offer.find("transport-wide-cc"), std::string::npos);
  EXPECT_NE(offer.find("transport-cc"), std::string::npos);
  EXPECT_NE(offer.find("OCTREE-SIM/90000"), std::string::npos);
}

TEST(SimulatedFrameSource, RejectsSubmissionOnReceiveOnlySession) {
  auto receiver = StartedSession(MediaDirection::kReceiveOnly);
  EXPECT_FALSE(receiver->SubmitSimulatedFrame(BuildTwoMillionByteFrame(1), 1'000).ok);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_twcc_negotiation_test
```

Expected: compile failure because `SubmitSimulatedFrame` and the scheduling source do not exist.

- [ ] **Step 3: Implement source and session attachment**

Add to `PeerConnectionSession`:

```cpp
virtual SessionStatus SubmitSimulatedFrame(
    SimulatedOctreeFrame frame, std::uint32_t rtp_timestamp) = 0;
virtual TransportFeedbackSnapshot transport_feedback() const = 0;
```

For sendonly startup, create one shared queue, feedback store, and `SimulatedFrameSource`; create `factory_->CreateVideoTrack(source, "octree-sim")`; attach it to the transceiver sender with `SetTrack`. `SubmitSimulatedFrame` partitions the deterministic frame, calculates the next-frame budget from the latest `SetRates`, admits P0/P1/P2 blocks, serializes admitted source bytes into one unit, queues it, then pushes a 2x2 I420 `VideoFrame` with the same RTP timestamp through `VideoBroadcaster`.

The source must not run its own cadence thread. Callers decide when a logical source frame is submitted, making tests deterministic.

- [ ] **Step 4: Run GREEN plus Task 10 lifecycle tests**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_twcc_negotiation_test volumetric_peer_connection_lifecycle_test volumetric_localhost_ice_dtls_test
ctest --preset windows-clang-runtime -R "TwccNegotiation|PeerConnectionLifecycle|LocalhostIceDtls" --output-on-failure
```

Expected: TWCC assertions pass; ICE/DTLS tests still pass without skips.

- [ ] **Step 5: Record checkpoint**

Save the negotiated OCTREE-SIM m-section excerpt in the progress document without logging credentials or fingerprints as reusable secrets.

## Task 6: RTCStats mapper with honest optional values

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/rtc/rtc_stats_mapper.h`
- Create: `bupt_volumetric/src/rtc/rtc_stats_mapper.cc`
- Create: `test/test_volumetric_metrics/rtc_stats_mapping_test.cc`
- Modify: `bupt_volumetric/CMakeLists.txt`
- Modify: `test/test_volumetric_metrics/CMakeLists.txt`

- [ ] **Step 1: Write RED tests with a synthetic report**

```cpp
TEST(RtcStatsMapper, UsesTransportSelectedPairAndPreservesMissingValues) {
  auto report = MakeReportWithTwoPairs("selected-pair");
  const RtcTransportObservation out = MapRtcStats(*report);
  EXPECT_EQ(out.selected_pair_rtt_ms, 12.5);
  EXPECT_EQ(out.available_outgoing_bitrate_bps, 3'000'000u);
  EXPECT_FALSE(out.loss_fraction.has_value());
}

TEST(RtcStatsMapper, ConvertsSecondsToMillisecondsAndAggregatesOutbound) {
  auto report = MakeReportWithOutboundAndRemoteInbound();
  const RtcTransportObservation out = MapRtcStats(*report);
  EXPECT_EQ(out.outbound_bytes, 3'000u);
  EXPECT_EQ(out.retransmitted_bytes, 200u);
  EXPECT_DOUBLE_EQ(*out.total_packet_send_delay_ms, 25.0);
  EXPECT_DOUBLE_EQ(*out.jitter_ms, 4.0);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_rtc_stats_mapping_test
```

Expected: compile failure because mapper API is missing.

- [ ] **Step 3: Implement mapper boundary**

Expose:

```cpp
[[nodiscard]] RtcTransportObservation MapRtcStats(
    const webrtc::RTCStatsReport& report);
```

Find `RTCTransportStats::selected_candidate_pair_id`, then resolve exactly that `RTCIceCandidatePairStats`. Sum video `RTCOutboundRtpStreamStats` counters; pair a video `RTCRemoteInboundRtpStreamStats` by local_id/ssrc when available. Convert all seconds fields to milliseconds. Clamp `fraction_lost` to `[0.0, 1.0]` only when present and finite. Leave unavailable members unset.

- [ ] **Step 4: Run GREEN**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_rtc_stats_mapping_test
ctest --preset windows-clang-runtime -R RtcStatsMapper --output-on-failure
```

Expected: mapper tests pass without relying on a live PeerConnection.

- [ ] **Step 5: Record checkpoint**

Document each source RTCStats field and its output unit in `docs/architecture/congestion-control.md`.

## Task 7: Export existing pacer delay through modern RTCStats

**Files:**

- Modify: `extern/webrtc/src/api/stats/rtcstats_objects.h`
- Modify: `extern/webrtc/src/stats/rtcstats_objects.cc`
- Modify: `extern/webrtc/src/pc/rtc_stats_collector.cc`
- Modify: `extern/webrtc/src/pc/rtc_stats_collector_unittest.cc`
- Modify: `bupt_volumetric/webrtc_patches/manifest.json`
- Modify: `bupt_volumetric/webrtc_patches/README.md`

- [ ] **Step 1: Audit the meaningful pre-patch diff**

```powershell
git -C extern/webrtc/src -c core.safecrlf=false diff --ignore-cr-at-eol --name-status
```

Expected: the known Task 8 FEC seam edits and pre-existing user edits remain; no stats files are modified yet.

- [ ] **Step 2: Add one failing upstream collector test**

Add a test that provides `Call::Stats{.pacer_delay_ms = 17}` and a best connection, collects the report, resolves the selected candidate pair, and asserts:

```cpp
ASSERT_TRUE(candidate_pair->pacer_delay.has_value());
EXPECT_DOUBLE_EQ(*candidate_pair->pacer_delay, 0.017);
```

Also create a non-best pair and assert its `pacer_delay` is absent.

- [ ] **Step 3: Run RED on the narrow upstream target**

```powershell
$env:DEPOT_TOOLS_WIN_TOOLCHAIN='0'
extern\webrtc\src\third_party\ninja\ninja.exe -C extern\webrtc\src\out\Default rtc_stats_collector_unittests
```

Expected: compile failure because `RTCIceCandidatePairStats::pacer_delay` is absent.

- [ ] **Step 4: Add the read-only stats field**

In `RTCIceCandidatePairStats` add:

```cpp
// Non-standard read-only observation of the current WebRTC pacer queue delay.
// Unit: seconds, matching other RTCStats duration fields.
std::optional<double> pacer_delay;
```

Register `AttributeInit("pacerDelay", &pacer_delay)`. In the existing `info.best_connection` block that exports call bandwidth, set:

```cpp
if (call_stats.pacer_delay_ms >= 0) {
  candidate_pair_stats->pacer_delay =
      static_cast<double>(call_stats.pacer_delay_ms) / 1000.0;
}
```

Do not change pacer, GoogCC, task queues, or send scheduling.

- [ ] **Step 5: Run GREEN and rebuild the archive**

```powershell
$env:DEPOT_TOOLS_WIN_TOOLCHAIN='0'
extern\webrtc\src\third_party\ninja\ninja.exe -C extern\webrtc\src\out\Default rtc_stats_collector_unittests
extern\webrtc\src\out\Default\rtc_stats_collector_unittests.exe --gtest_filter=*PacerDelay*
extern\webrtc\src\third_party\ninja\ninja.exe -C extern\webrtc\src\out\Default webrtc
```

Expected: focused upstream test passes and `obj/webrtc.lib` is freshly rebuilt from the patched sources.

- [ ] **Step 6: Update patch provenance**

Add the four stats files, purpose, test target, and “read-only/no algorithm change” invariant to the patch manifest and README. Re-run the meaningful diff and verify no unrelated file appeared.

## Task 8: Asynchronous stats collection through the session

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h`
- Modify: `bupt_volumetric/src/rtc/peer_connection_session.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/rtc_stats_mapper.h`
- Modify: `bupt_volumetric/src/rtc/rtc_stats_mapper.cc`
- Modify: `test/test_volumetric_metrics/rtc_stats_mapping_test.cc`

- [ ] **Step 1: Write RED lifecycle test for asynchronous collection**

```cpp
TEST(PeerConnectionStats, PublishesOneSnapshotAndIgnoresLateCallbackAfterStop) {
  auto session = ConnectedLoopbackSender();
  ASSERT_TRUE(session->CollectTransportStats().ok);
  ASSERT_TRUE(WaitUntil([&] {
    return session->transport_feedback().outbound_packets.has_value();
  }, 5'000));
  EXPECT_TRUE(session->Stop().ok);
  EXPECT_FALSE(session->CollectTransportStats().ok);
}
```

- [ ] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_rtc_stats_mapping_test
```

Expected: compile failure because `CollectTransportStats` is absent.

- [ ] **Step 3: Implement a one-shot stats API**

Add:

```cpp
virtual SessionStatus CollectTransportStats() = 0;
```

Implement `RTCStatsCollectorCallback` with shared ownership of `TransportFeedbackStore` and `stopped`; map the report then call `store->OnStats`. The session method invokes `peer_connection_->GetStats(callback)` and never blocks a WebRTC thread. Do not add an internal repeating timer; applications/tests choose the 500 ms sampling cadence.

Extend mapper handling for the new `pacer_delay` seconds field and convert it to milliseconds.

- [ ] **Step 4: Run GREEN plus lifecycle regression**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_rtc_stats_mapping_test volumetric_peer_connection_lifecycle_test
ctest --preset windows-clang-runtime -R "RtcStats|PeerConnectionLifecycle" --output-on-failure
```

Expected: stats callback and shutdown tests pass.

- [ ] **Step 5: Record checkpoint**

Document callback ownership and the absence of a hidden polling thread.

## Task 9: Real localhost RTP/SRTP media flow without rendering

**Files:**

- Create: `test/test_volumetric_rtc/localhost_media_flow_test.cc`
- Modify: `test/test_volumetric_rtc/CMakeLists.txt`
- Modify only as failures require: session/source/codec files from Tasks 4–8

- [ ] **Step 1: Add the failing end-to-end test**

Reuse the proven in-memory SDP/ICE relay from `localhost_ice_dtls_test.cc`, then:

```cpp
for (std::uint64_t frame_id = 1; frame_id <= 30; ++frame_id) {
  ASSERT_TRUE(sender->SubmitSimulatedFrame(
      GenerateTwoMillionByteFrame(frame_id, 0x5eed),
      static_cast<std::uint32_t>(frame_id * 3'000)).ok);
  std::this_thread::sleep_for(std::chrono::milliseconds(34));
}
ASSERT_TRUE(sender->CollectTransportStats().ok);
ASSERT_TRUE(receiver->CollectTransportStats().ok);
ASSERT_TRUE(WaitUntil([&] {
  return sender->transport_feedback().outbound_packets.value_or(0) > 0 &&
         receiver_completions.load() > 0;
}, 10'000));
EXPECT_GT(rate_update_count.load(), 0u);
EXPECT_TRUE(sender->transport_feedback().pacer_delay_ms.has_value());
```

- [ ] **Step 2: Run RED and classify the first real failure**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_localhost_media_flow_test
ctest --preset windows-clang-runtime -R LocalhostMediaFlow --output-on-failure
```

Expected: the test fails at the first incomplete seam rather than being skipped. Record whether the failure is track attachment, codec callback, RTP counter, decoder callback, or stats mapping.

- [ ] **Step 3: Fix one seam at a time with a regression test**

For each observed failure, first add the smallest reproducing assertion to `octree_sim_media_test.cc`, `twcc_negotiation_test.cc`, or `rtc_stats_mapping_test.cc`; watch it fail; apply the smallest source change; rerun the focused test before the localhost test.

- [ ] **Step 4: Run GREEN repeatedly**

```powershell
ctest --preset windows-clang-runtime -R LocalhostMediaFlow --repeat until-fail:10 --output-on-failure
```

Expected: ten consecutive passes, no skips, sender outbound packets/bytes grow, receiver completion count grows, and at least one nonzero WebRTC rate callback is observed.

- [ ] **Step 5: Record checkpoint**

Add every discovered integration defect and its regression test name to `docs/progress.md` as required by the project testing policy.

## Task 10: Capacity step and semantic admission evidence

**Files:**

- Create: `test/test_volumetric_congestion/capacity_step_test.cc`
- Modify: `test/test_volumetric_congestion/CMakeLists.txt`
- Modify: `bupt_volumetric/include/bupt_volumetric/metrics/metric_event.h`
- Modify only if needed: existing transport policy files

- [ ] **Step 1: Add RED tests for direction and priority order**

```cpp
TEST(CapacityStep, WebRtcTargetsDrivePriorityOrderedAdmission) {
  const std::array<std::uint64_t, 4> targets = {
      8'000'000, 3'000'000, 800'000, 4'000'000};
  const auto results = RunAdmissionForTargets(targets, 30.0);
  EXPECT_GT(results[0].p2_bytes, results[1].p2_bytes);
  EXPECT_GE(results[1].p1_bytes, results[2].p1_bytes);
  EXPECT_GE(results[2].p0_bytes, results[2].p1_bytes);
  EXPECT_GT(results[3].total_bytes(), results[2].total_bytes());
}
```

The helper must feed targets through `TransportFeedbackStore::OnRateUpdate`, then use `TransportBudgetCalculator` and the real `SemanticAdmissionController`; it must not pass target bytes directly to a test-only admission algorithm.

- [ ] **Step 2: Run RED**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_capacity_step_test
```

Expected: compile failure because the metric schema and application orchestration result are absent.

- [ ] **Step 3: Add metric provenance and minimal orchestration**

Define:

```cpp
enum class MetricProvenance : std::uint8_t {
  kObserved,
  kDerived,
  kEstimated,
};

struct CongestionMetricEvent final {
  std::string run_id;
  std::uint64_t monotonic_time_ms = 0;
  std::uint64_t target_bitrate_bps = 0;
  std::uint64_t admitted_p0_bytes = 0;
  std::uint64_t admitted_p1_bytes = 0;
  std::uint64_t admitted_p2_bytes = 0;
  std::uint64_t dropped_p0_bytes = 0;
  std::uint64_t dropped_p1_bytes = 0;
  std::uint64_t dropped_p2_bytes = 0;
  std::optional<double> pacer_delay_ms;
  std::optional<std::uint64_t> actual_sent_bitrate_bps;
  std::uint64_t estimated_transport_overhead_bytes = 0;
};
```

Keep provenance fixed by field definition: pacer is observed, actual bitrate is derived, overhead is estimated. Reuse production partition/admission paths to produce per-class admitted and dropped byte counts.

- [ ] **Step 4: Run GREEN and existing congestion suite**

```powershell
cmake --build --preset windows-clang-runtime --target volumetric_capacity_step_test
ctest --preset windows-clang-runtime -R "CapacityStep|TransportBudget|SemanticAdmission|WebRtcRateCallback" --output-on-failure
```

Expected: downward steps reduce admission in P2-then-P1 order and recovery increases total admitted bytes.

- [ ] **Step 5: Add deterministic WebRTC BWE evidence**

Locate the pinned checkout's existing call/fake-network congestion-control test target with:

```powershell
rg -n "SetRates|BuiltInNetworkBehaviorConfig|link_capacity|capacity" extern/webrtc/src/video extern/webrtc/src/call extern/webrtc/src/test -g "*test*.cc"
```

Extend the narrowest existing test that already owns a fake network and encoder rate observer with the 8 → 3 → 0.8 → 4 Mbps sequence. Assert direction over stable windows, not an exact instantaneous bitrate. Run that target with bundled Ninja and record the exact binary/filter. Do not create a second application congestion controller.

- [ ] **Step 6: Record checkpoint**

Link the deterministic upstream BWE test and project admission test in `docs/progress.md`; state explicitly that one proves GoogCC direction and the other proves project policy consumption.

## Task 11: Full verification, documentation, and Task 12 gate

**Files:**

- Modify: `bupt_volumetric/docs/architecture/congestion-control.md`
- Modify: `bupt_volumetric/docs/architecture/webrtc-patch-boundary.md`
- Modify: `bupt_volumetric/docs/progress.md`
- Modify: `bupt_volumetric/webrtc_patches/manifest.json`

- [ ] **Step 1: Reconfigure both presets from source metadata**

```powershell
cmake --preset windows-clang-release
cmake --preset windows-clang-runtime
```

Expected: both configure without missing source or target errors.

- [ ] **Step 2: Build the complete project test surfaces**

```powershell
cmake --build --preset windows-clang-release
cmake --build --preset windows-clang-runtime
```

Expected: both builds succeed; runtime links the freshly rebuilt patched `webrtc.lib`.

- [ ] **Step 3: Run full suites**

```powershell
ctest --preset windows-clang-release --output-on-failure
ctest --preset windows-clang-runtime --output-on-failure
```

Expected: all tests pass, zero failed, zero skipped; Task 7–10 tests remain green.

- [ ] **Step 4: Run stability and targeted upstream suites**

```powershell
ctest --preset windows-clang-runtime -R "LocalhostIceDtls|LocalhostMediaFlow" --repeat until-fail:20 --output-on-failure
extern\webrtc\src\out\Default\rtc_stats_collector_unittests.exe --gtest_filter=*PacerDelay*
extern\webrtc\src\out\Default\modules_unittests.exe --gtest_filter=*SemanticProtection*
extern\webrtc\src\out\Default\video_rtp_video_stream_receiver2_unittest_bin.exe --gtest_filter=*SemanticPayloadObserver*
```

Expected: stability loop and all targeted upstream patch tests pass.

- [ ] **Step 5: Audit scope and provenance**

```powershell
git -C extern/webrtc/src -c core.safecrlf=false diff --ignore-cr-at-eol --name-status
git -C extern/webrtc/src -c core.safecrlf=false diff --ignore-cr-at-eol -- api/stats/rtcstats_objects.h stats/rtcstats_objects.cc pc/rtc_stats_collector.cc pc/rtc_stats_collector_unittest.cc
```

Expected: stats diff contains only the optional field, collector assignment, and tests; pre-existing files remain untouched except explicitly documented Task 8 edits.

- [ ] **Step 6: Write the acceptance record**

Update documentation with:

- exact source-to-binary build commands;
- complete test counts and zero-skip evidence;
- negotiated TWCC SDP evidence;
- rate callback, outbound/inbound counter, pacer delay, and actual bitrate samples;
- observed/derived/estimated labels;
- every defect discovered during testing and its regression test;
- explicit statement that data and codecs are simulated and no rendering exists;
- Task 12 gate: native repair transmission may start only after every Task 11 acceptance item is green.

## Self-review record

- Spec coverage: Tasks 1–10 cover domain feedback, simulated media, TWCC negotiation, stats, pacer observation, localhost media flow, capacity response, semantic admission, and metric provenance; Task 11 covers regression and provenance evidence.
- Dependency direction: only `rtc` adapters include WebRTC headers; transport policy remains framework-independent.
- Scope: no rendering, real point-cloud codec, native FEC sender, post-FEC receiver, or alternate media socket is introduced.
- Type consistency: `EncoderRateUpdate`, `RtcTransportObservation`, `TransportFeedbackSnapshot`, `TransportFeedbackStore`, `SimulatedAccessUnit`, and `SimulatedAccessUnitQueue` keep the same names throughout all tasks.
- Workspace safety: commits are replaced by verification checkpoints because the application workspace is not a Git repository; the vendored WebRTC repository is never cleaned or reset.
