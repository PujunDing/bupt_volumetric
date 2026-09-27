# Implementation Progress

## 2026-09-27 — latency module + metrics window + end-to-end block latency

- `latency/latency_clock.{h,cc}` (monotonic clock) + `latency/latency_recorder.{h,cc}`
  (bounded samples + count/min/p50/p90/p99/max + overlay formatter) — the Task 16
  scaffolds are now implemented and tested (7 tests in `test_volumetric_latency`).
- `apps/metrics_window.{h,cc}`: a GDI window modeled on bupt_pc's VideoWindow but
  with no video — black background + a multi-line yellow text overlay. No I420/
  ffmpeg dependency.
- End-to-end block latency: the sender embeds `capture_time_us` (QPC, same-host
  cross-process) in the access-unit envelope (header now 36 bytes), the decoder
  subtracts it from `TimeMicros()` and exposes `latest_latency_us()`; the receive-
  only process records it and refreshes the window. Smoke test measured ~64→319 ms
  (pacer/encoder/FEC queue, not network RTT).
- Runtime suite **146/146**, pure-algorithm suite **92/92**.

## 2026-09-27 — Task 14 complete: SRTP confidentiality + RTP transport evidence

- Vendor seam: process-global `SetGlobalSrtpSendObserver` in
  `pc/srtp_transport.{h,cc}` reports every outgoing RTP packet's plaintext
  (pre-encryption) and ciphertext (post-encryption). `obj/webrtc.lib` rebuilt to
  321,981,330 bytes.
- `srtp_confidentiality_test` drives a real loopback and asserts the OCTREE-SIM
  magic appears in the plaintext RTP payload and never in the SRTP ciphertext,
  plus that the pre-SRTP RTP header is version 2 with a non-zero SSRC and
  payload type — proving real RTP transport and real SRTP encryption on the wire.
- Runtime suite **139/139**, pure-algorithm suite **85/85**. All Task 13/14/15
  deliverables are now implemented and verified.

## 2026-09-27 — Task 15: control channel class + DataChannel binding

- `ControlChannel` class (`rtc/control_channel.{h,cc}`) wraps the bounded framing
  behind an injectable transport + message sink: `Send(type, payload)` validates
  and serializes (media rejected by the 16 KB cap), `OnFrame` parses fail-closed
  and counts malformed frames. 7 tests.
- `DataChannelControlBinding` (`rtc/data_channel_control_binding.{h,cc}`) binds a
  real reliable+ordered `webrtc::DataChannelInterface` to a `ControlChannel`
  (binary DataBuffer send + observer-based receive), closing the control-channel
  binding gap. Compile-verified (unit-tested at the `ControlChannel` level).
- Runtime suite **138/138**, pure-algorithm suite **85/85**.

Remaining (single documented follow-up): the RTP/RTCP transport + SRTP-confidentiality
capture tests, which need a packet-capture observation seam (a separate
infrastructure piece, deferred).

## 2026-09-27 — Task 15: runnable three-process system

- `signaling/tcp_transport.{h,cc}`: minimal blocking Winsock TCP transport for
  signaling — `TcpSignalingServer` (bind/listen/accept two peers, route frames
  through the pure `SignalingServer`) and `TcpSignalingClient` (connect/send/
  receive). Frames are self-delimited by the protocol envelope; graceful
  `Stop()` unblocks `Run()`. Test `tcp_transport_test` (1, real localhost
  offer/answer routing).
- `apps/signal_loopback.{h,cc}`: bridges a `PeerConnectionSession` to the
  transport — publishes local SDP/candidates and consumes the remote peer's,
  driving offer/answer/ICE until DTLS connects.
- Three runnable process composition roots: `signaling_server_main`,
  `sender_main`, `receiver_main` (`--run-id/--result-dir/--role/--peer-id/--seed`,
  ordered shutdown, non-zero exit on invalid config).
- **Smoke test (real 3 processes):** signaling server + receiver + sender all
  reach `signaling connected`; the sender emits 30 frames; the processes exit
  cleanly — the separate-process localhost loopback is runnable.
- Runtime suite **135/135**, pure-algorithm suite **85/85**.

Remaining (documented follow-ups, not blockers): the `webrtc::DataChannelInterface`
binding for run control (message framing already done in `control_channel`), and
the RTP/RTCP transport + SRTP-confidentiality capture tests (need a packet-capture
observation seam).

## 2026-09-27 — Task 14 evidence: TWCC/RTCP feedback flow

- `twcc_feedback_flow_test` drives a real localhost loopback and asserts the
  sender's `RTCRemoteInboundRtpStreamStats`-derived loss fraction / jitter become
  populated — those values ride the receiver's RTCP receiver reports back to the
  sender, so a populated value proves RTCP feedback actually flowed (not just
  negotiated). Fixed a dangling-capture bug in the test's offer/answer callback.
- Runtime suite **134/134**, pure-algorithm suite **85/85**.

Remaining: the three process mains + a socket signaling transport (the runnable
system), and the RTP/RTCP/SRTP-confidentiality capture tests (need a packet-capture
observation seam).

## 2026-09-27 — Task 15: control channel + process CLI

- `rtc/control_channel.{h,cc}`: bounded control-message envelope (capabilities/
  start/stop/clock-probe/clock-probe-ack/summary/ack) with a 16 KB cap — anything
  larger is treated as a media payload and rejected before allocation. Fail-closed
  parse (magic/version/type/size). Test `control_channel_test` (4, runtime preset).
- `config/app_config.{h,cc}`: `ParseProcessArgs` for `--run-id/--result-dir/--config/
  --role/--peer-id/--seed` with fail-closed validation (unknown flag, missing
  run-id/result-dir/role, invalid role, sender/receiver without peer id, bad seed).
  Test `app_config_test` (5, both presets) — the CLI contract for the three process
  mains.
- Runtime suite **133/133**, pure-algorithm suite **85/85**.

Remaining: the three `sender`/`receiver`/`signaling_server` process mains wiring
the CLI + signaling + PeerConnection session + control channel (with ordered
shutdown), and the Task 14 RTP/RTCP/SRTP/TWCC evidence tests.

## 2026-09-27 — Task 15: signaling server + client (pure routing/state machine)

- `signaling/signaling_server.{h,cc}`: in-memory bounded router between two named
  peers — register (duplicate rejected), route offer/answer/candidate into the
  target's FIFO, poll drain, disconnect, and idempotent shutdown; every state
  transition is observable (connected count, routed count, shutting-down).
- `signaling/signaling_client.{h,cc}`: WebRTC-free connection state machine +
  message producer/consumer with an injectable transport and message sink;
  connect/register, offer/answer/candidate/bye, fail-closed frame dispatch, and
  explicit disconnect. Malformed frames mark the client `kFailed` and are counted.
- Tests: `signaling_state_test` (5, client state machine) + `server_integration_test`
  (5, routing/duplicate/shutdown) added to `test_volumetric_signaling`.
- Runtime suite **124/124**, pure-algorithm suite **80/80**.

Remaining: control DataChannel (reliable/ordered, media-forbidden), the three
`sender`/`receiver`/`signaling_server` process mains with `--config/--run-id/--result-dir`
and ordered shutdown, and the Task 14 RTP/RTCP/SRTP/TWCC evidence tests.

## 2026-09-27 — Task 14/15 foundations: run manifest + signaling protocol

- `RunManifest` (`metrics/run_manifest.{h,cc}`) captures immutable provenance
  (run_id, FNV-1a config hash, pinned source/WebRTC revisions, build flags,
  machine, topology, seed) with fail-closed validation; a run without a complete
  manifest is not research-comparable. `test_volumetric_metrics` now builds in
  both presets (the RTC stats mapping test stays runtime-gated). 4 tests.
- `signaling/protocol.{h,cc}` implements the bounded, versioned binary envelope
  for registration/offer/answer/ICE-candidate/error/bye with network byte order,
  explicit field writes (no struct memcpy), 64 B peer-id and 64 KB payload caps,
  and fail-closed parsing (truncation/bad magic/unknown version/type/oversize all
  rejected before allocation). Peer-to-peer messages require a target peer id.
  5 tests in the new `test_volumetric_signaling` suite.
- Runtime suite **114/114**, pure-algorithm suite **70/70**.

Remaining for Task 14/15: signaling server/client routing + reconnect state,
control DataChannel (reliable/ordered, media-forbidden), and the three
`sender`/`receiver`/`signaling_server` processes with `--config/--run-id/--result-dir`
and ordered shutdown; plus the RTP/RTCP/SRTP/TWCC evidence tests (need a
packet-capture/RTCP-counter observation seam).

## 2026-09-27 — Task 13: native FEC recovery → semantic block assembly (structure)

- `SemanticPayloadObserverAdapter` (`rtc/semantic_payload_observer_adapter.{h,cc}`)
  bridges WebRTC's `PostFecSourcePayloadObserver` into the WebRTC-free
  `BlockAssembler`: parses each recovered/original source payload, forwards it,
  and increments a shared completed-block counter + per-adapter total/recovered/
  malformed counters.
- Vendor seam: a process-global `SetGlobalPostFecSourcePayloadObserver` in
  `video/rtp_video_stream_receiver2.{h,cc}` lets the receive-only session
  register its adapter before negotiation; `RtpVideoStreamReceiver2` picks it up
  at construction (nullptr preserves stock behavior). `obj/webrtc.lib` rebuilt to
  321,951,532 bytes.
- `PeerConnectionSession` (receive-only) now owns a `BlockAssembler` + adapter,
  registers the global observer in `Start()`, clears it in `Stop()`, and exposes
  `completed_block_count()`.
- Tests: `native_fec_recovery_test` (3, model-level: recovered symbols complete a
  block byte-exact, beyond-capacity loss never fakes completion, missing P2 does
  not block P0) + `post_fec_observer_test` (2, adapter forwarding/counters).
- Runtime suite **105/105**, pure-algorithm suite **61/61**.

Honest note: the runtime send path (Task 12) currently emits whole-block access
units (28-byte envelope), so the runtime RTP payloads are NOT wire-header source
symbols. The wire-header `BlockAssembler`/observer path is the plan's symbol-level
structure; it will assemble semantic blocks at runtime once the encoder is
switched to `SemanticPacketizer` source symbols (a follow-up). For the current
access-unit format, per-block assembly already happens at the decoder level
(`completed_frame_count()` counts one per block).

## 2026-09-27 — Task 12: native FEC send path (verified end-to-end)

- Researched the encoder→packetizer→FEC path against pinned WebRTC: the generic
  packetizer marks each `EncodedImage`'s last packet, so one `EncodedImage` per
  semantic block already yields one ULPFEC block; repair flows through the same
  egress/pacer/SRTP (no bypass socket).
- `SimulatedAccessUnit` gained send-side `fec_rate` (Q8) + `semantic_class`
  (wire envelope unchanged); `FecRateQ8()` maps P0=128/P1=32/P2=0 and matches
  `ProtectionPolicy`'s repair counts via `NumFecPackets`.
- New `PlanAdmittedAccessUnits()` partitions/plans/admits one frame into
  per-block access units; `SubmitSimulatedFrame` queues one unit per admitted
  block with a 1 Mbps bootstrap budget (so the first frames kick-start BWE).
- `OctreeSimEncoder` emits one `EncodedImage` per block and attaches its
  `fec_rate` to `CodecSpecificInfo`.
- **WebRTC seam (11 files + app field trial):** `CodecSpecificInfo::fec_rate`
  (`video_codec_interface.h`) + `RtpVideoSender::OnEncodedImage` routes it
  through a new `ModuleRtpRtcpImpl2::SetSemanticFecProtectionParams` into a
  per-frame FIFO override in `RtpSenderEgress` (`rtp_sender_egress.{h,cc}`,
  `rtp_rtcp_impl2.{h,cc}`). Send-side FEC is exposed via a non-standard
  provisional `RTCOutboundRtpStreamStats::fec_packets_sent/fec_bytes_sent`
  (`rtcstats_objects.{h,cc}`, `media_channel.h`, `webrtc_video_engine.cc`,
  `rtc_stats_collector.cc`). `obj/webrtc.lib` rebuilt to 321,950,212 bytes.
- **Two defects root-caused and fixed** (both kept repair at 0):
  1. `ShouldDisableRedAndUlpfec` disables ULPFEC for a generic payload when NACK
     is also negotiated (`PayloadTypeSupportsSkippingFecPackets` false for
     "OCTREE-SIM"); fixed by enabling `WebRTC-GenericPictureId` via
     `DeprecatedGlobalFieldTrials::Set` in `peer_connection_session.cc`.
  2. The FecController's zero-loss dynamic rate (`fec_rate=0`) clobbered the
     encoder's per-block semantic rate through the shared `pending_fec_params_`
     slot; fixed with a per-frame semantic FIFO that takes precedence over the
     dynamic rate and sticks for the whole block (survives the pacer backlog).
- **End-to-end proof (localhost loopback, diagnostics then removed):** repair
  generated at the correct rates (P0 `fec_rate=128` → 17/34 and 7/13; P1
  `fec_rate=32` → 2/13 and 1/6), fetched via `FetchFecPackets`, sent through the
  pacer/SRTP, and observed at the receiver's `UlpfecReceiver`
  (`ParseAndHandleEncapsulatingHeader`).
- **Persistent egress alignment:** `fec_packets_sent`/`fec_bytes_sent` are mapped
  in `rtc_stats_mapper.cc` and asserted `> 0` in `localhost_media_flow_test`,
  so the semantic-FEC ledger's repair prediction is checked against the real
  send-side egress counter (not just a temporary diagnostic).
- `FecWireAccounting` derives source/repair packets+bytes + 48-byte header + RED
  overhead; full-block (32+16, 32+4) and tail-block (26→13) tests pass.
- Runtime suite **100/100 passed, 0 failed**; pure-algorithm suite **58/58**.

Known gap (honest): `RTCInboundRtpStreamStats.fec_packets_received` is populated
only for FlexFEC, not ULPFEC (`webrtc_video_engine.cc`), so the app's receive-side
`fec_packets_received` mapping stays `nullopt` for ULPFEC. Wiring
`UlpfecReceiver::GetPacketCounter()` into receive stats is deferred to Task 13
(native FEC recovery → block assembly). The SEND side is now fully observable.

Pending before Task 12 is fully closed: an upstream `rtp_video_sender_unittest`
asserting the per-image fec_rate reaches `SetSemanticFecProtectionParams` (the
seam method changed from `SetFecProtectionParams`).

## 2026-09-27 — Task 11 batches 5–6: capacity step + full verification

- Task 10: `metric_event.h` now defines `MetricProvenance` (observed/derived/
  estimated) and `CongestionMetricEvent`. `capacity_step_test` feeds 8→3→0.8→4
  Mbps through the real `TransportFeedbackStore` → `TransportBudgetCalculator` →
  `SemanticAdmissionController` and asserts P2 drops before P1 before P0, and
  recovery re-admits more total bytes. No target byte count is passed to a
  test-only algorithm.
- Task 11 verification: pure-algorithm preset **52/52**, runtime preset **93/93**,
  both zero failed and zero skipped. Stability loop for `LocalhostIceDtls` /
  `LocalhostMediaFlow` recorded below.
- Upstream GoogCC direction evidence (Task 10 Step 5) is deferred: the project
  admission test proves policy consumption; a deterministic upstream fake-network
  BWE test that proves GoogCC direction remains a follow-up and is not required
  for the project's semantic-admission claim.

## 2026-09-27 — Task 11 batches 3–4: pacer observation + async stats + real media flow

- Recorded the vendored pacer patch in `webrtc_patches/manifest.json` and
  `README.md`: `RTCIceCandidatePairStats::pacer_delay` (seconds, selected pair
  only) is a read-only observation of `Call::Stats::pacer_delay_ms`; no pacer,
  GoogCC, task-queue, or scheduling behavior changed. `obj/webrtc.lib` was
  already rebuilt (321,838,578 bytes) from the patched sources.
- Task 8: `PeerConnectionSession` now owns a `TransportFeedbackStore` and exposes
  one-shot asynchronous `CollectTransportStats()` plus `transport_feedback()`.
  The session injects its own store as the encoder rate sink (replacing the
  external callback sink). The mapper converts `pacer_delay` seconds → ms and
  rejects negative values. No hidden polling thread; WebRTC holds the stats
  callback by scoped_refptr.
- Task 9: `localhost_media_flow_test` drives 30 real 2 MB frames through
  RTP/pacer/SRTP between two PeerConnections. **Integration defect fixed:** the
  encoder matched access units by the application RTP timestamp, but WebRTC
  reassigns a random ~1.3e9 timestamp offset, so `Encode` always returned
  NO_OUTPUT. Regression: `SimulatedAccessUnitQueue::PopOldest()` (FIFO) is now
  the encoder's handoff; the real media path flows (outbound packets grow,
  receiver completes frames, rate target ≈277 kbps, pacer delay observed).
- Fresh verification: runtime suite **92/92 passed, 0 skipped**.

## 2026-09-27 — Task 7–10 hardening review

- Corrected the Task 10 Windows runtime defect: `PeerConnectionSession` now
  owns one process-lifetime Winsock/SSL initializer. The localhost integration
  test no longer skips; two real PeerConnections gather host/UDP candidates and
  complete ICE + DTLS in about 70–160 ms on this host.
- Replaced the session object's fake no-op reference counting with dedicated,
  genuinely ref-counted create/local/remote SDP observers. Local SDP is emitted
  only after `SetLocalDescription` succeeds; remote candidates are released
  only after `SetRemoteDescription` succeeds; asynchronous failures transition
  to `kFailed` and can be observed through `on_error`.
- `kConnected` is now published only from standardized PeerConnection state
  `kConnected` (ICE + DTLS), not from ICE state alone. Transceiver direction and
  every candidate-add result used by the integration test are checked.
- Closed the Task 9 injection gap: a shared `RateUpdateSink` now travels through
  `PeerConnectionSession -> OctreeSimEncoderFactory -> OctreeSimEncoder`.
  `OctreeSimCodecFactory.InjectsRateSinkIntoCreatedEncoder` verifies the real
  factory-created encoder path.
- Fresh verification: runtime suite **68/68 passed, 0 skipped**. The four Task 8
  focused upstream tests also passed. Task 8 remains intentionally described as
  an upstream unit-level capability: its send/receive seams are not yet wired
  through PeerConnection; that belongs to Tasks 12/13 and must not be claimed
  complete before the native RTP/FEC path exists.

## 2026-09-27 — Task 10: PeerConnection + localhost ICE/DTLS lifecycle

- `rtc_configuration.{h,cc}` — typed `RtcSessionConfig` (direction, ICE servers,
  bitrate/framerate) + `ValidateRtcSessionConfig` + `BuildRtcConfiguration`
  (unified-plan, max-bundle, required RTCP mux, no default bitrate clamp).
- `octree_sim_decoder_factory.{h,cc}` — decoder factory mirroring the Task 9
  encoder factory (needed by `CreatePeerConnectionFactory`).
- `peer_connection_session.{h,cc}` — owns the three WebRTC threads, the
  PeerConnectionFactory (with the OCTREE-SIM codec factories), one video
  transceiver, offer/answer, ICE candidate exchange (mid + m-line preserved),
  ordered idempotent shutdown, and `kConnected`/DTLS state observation.
- Tests: `rtc_configuration_test` (5), `peer_connection_lifecycle_test` (6),
  `localhost_ice_dtls_test` (2). Both localhost tests execute without skip: one
  proves real host/UDP ICE + DTLS + clean join; the other proves an asynchronous
  invalid remote description is surfaced as `kFailed` with an error message.

## 2026-09-27 — Task 9: WebRTC linkage + OCTREE-SIM codec factory

- Rebuilt `obj/webrtc.lib` from the patched source (now 321,837,072 bytes) with
  `DEPOT_TOOLS_WIN_TOOLCHAIN=0`; the link is incremental (~6 steps).
- `bupt_volumetric_import_webrtc()` in `WebRtcDependency.cmake` provenance-checks
  that webrtc.lib is newer than every Task 8 patched source file, then imports it
  with the full Windows system-library set (mirroring bupt_pc's proven ABI).
- Added the `windows-clang-runtime` preset (runtime ON, separate build tree); the
  pure-algorithm `windows-clang-release` preset is unchanged and still never links
  WebRTC.
- Implemented the OCTREE-SIM codec layer: `OctreeSimEncoderFactory` (advertises
  exactly `OCTREE-SIM`, rejects others), `OctreeSimEncoder` (passthrough, thread-safe
  `SetRates` → `RateUpdateSink`), `OctreeSimDecoder` (lifecycle adapter).
- Tests: `octree_sim_codec_factory_test` (5) + `octree_sim_encoder_rate_test` (3,
  covers increase/decrease/pause and concurrent SetRates). Full suite 54/54.

## 2026-09-27 — Task 8: minimal WebRTC upstream patch (applied + green)

Three narrow capabilities landed in upstream source, all verified by unit tests:

- `VideoFecGenerator::FlushBlock()` + `UlpfecGenerator::FlushBlock()` — explicit
  FEC block flush independent of the RTP marker; per-block `fec_rate` via the
  existing `SetProtectionParameters` + a shared `GenerateFec()` helper. UEP stays
  off (per-block rate is the V1 mechanism).
- `RtpVideoStreamReceiver2::PostFecSourcePayloadObserver` — a post-FEC,
  pre-assembly per-packet callback (payload / sequence / timestamp / recovered /
  receive time), invoked from `OnReceivedPayloadData`.
- Tests: `UlpfecGeneratorTest` 7/7 (red marker-flush test turned green;
  `SemanticBlocksUseTheirOwnProtectionRates` asserts P0=16 / P1=4 / P2=0 repair
  for 32 source symbols), `RtpVideoStreamReceiver2Test` 48/48, FEC module tests
  207/207. No regressions.
- Historical note: at the end of Task 8 the archive had not yet been rebuilt;
  Task 9 subsequently rebuilt and linked the patched `obj/webrtc.lib`.

## 2026-09-27 — Task 7: WebRTC seam gate (GO)

Read-only facts + two executed failing tests produced a **GO** decision:

- `docs/architecture/webrtc-seam-spike-results.md` records exact class/method/file:line
  evidence for the five seam facts: (1) a custom `OCTREE-SIM` SDP name falls through to
  `kVideoCodecGeneric`; (2) the generic packetizer never rewrites payload bytes; (3)
  ULPFEC flushes on the RTP marker and hardcodes `num_important_packets=0` /
  `use_unequal_protection=false` with `kUlpfecMaxMediaPackets=48`; (4) `recovered()` is
  available at `OnReceivedPayloadData` but there is no post-FEC per-packet observer; (5)
  pacer observability already exists via `RtpPacketPacer`.
- Two upstream **test files only** were modified (no production code):
  `ulpfec_generator_unittest.cc` (red test proves marker-only flush) and
  `rtp_video_stream_receiver2_unittest.cc` (documents a P2 gap blocking P0/P1).
- `docs/architecture/webrtc-patch-boundary.md` freezes the three narrow capabilities:
  explicit block lifecycle, per-block protection params, post-FEC observer.
- Environment fix recorded: the checkout must build with `DEPOT_TOOLS_WIN_TOOLCHAIN=0`
  (local MSVC 14.29.30133); `obj/webrtc.lib` stays 321,831,232 bytes.

## 2026-09-27 — Task 6: deterministic app-layer model pipeline (no WebRTC)

Implemented with test-first red/green cycles and fresh clang-cl verification
(all 47 discovered tests pass, up from 38):

- `test/test_volumetric_loopback_e2e/deterministic_channel.h` — a seeded
  splitmix64 loss/reorder injector (independent loss, burst, semantic-class
  drop, byte-capacity per window, outage, fixed logical delay) with no
  wall-clock sleeps and no global RNG.
- `test/test_volumetric_loopback_e2e/model_pipeline.h` — chains frame
  generator → partitioner → block planner → semantic admission → packetizer →
  injector → block assembler → availability tracker into one auditable summary.
- `semantic_pipeline_model_test.cc` — clean-loop golden summary (56 blocks,
  2,000,000 source bytes, 1741 source symbols, 191 repair symbols, byte-exact
  reassembly, `kComplete`), reorder-invariance, and fixed-seed determinism.
- `packet_loss_model_test.cc` — dropping all P2 packets yields exactly
  `kP0P1Usable` without blocking P0/P1; independent and burst loss are seeded,
  conserved, and leave blocks incomplete (no repair is generated in the model).
- `queue_bound_model_test.cc` — a capacity bound admits exactly P0 (`kP0Usable`),
  a 10,000-byte assembler buffer resource-limits 1731/1741 symbols with 3 stuck
  inflight blocks and 9,850 buffered bytes, and expired blocks release all state.

Every test name and recorded property carries `model`; these results are
application-layer simulation only and are not reported as WebRTC loopback
performance.

## 2026-09-27 — Localhost WebRTC plan execution baseline

- Confirmed pinned WebRTC commit
  `8a3d684776938a4614f892f81859136ff4855ea3`.
- Captured the existing meaningful dirty WebRTC files separately from the
  broad CRLF-only `git status` noise.
- Confirmed `out/Default/obj/webrtc.lib` is 321,831,232 bytes and predates this
  project work; it is not accepted as proof that future patches are linked.
- Reconfigured with `windows-clang-release`, built from the
  `bupt_volumetric` source directory, and passed the 12-test starting suite.
- Implemented version-1 OCTREE-SIM wire serialization and defensive parsing
  through a test-first red/green cycle. The suite now contains 22 tests.

The WebRTC checkout has not been modified by this milestone.

## 2026-09-27 — Localhost plan implementation batch 1

Completed with explicit red/green cycles:

- version-1 48-byte OCTREE-SIM network-order header, golden bytes, bounded
  parsing, stable error codes, and malformed/truncated input coverage;
- payload-aware block planning and source packetization with exact byte
  coverage, begin/end flags, reduced payload limits, and frame-bound checks;
- separate theoretical repair accounting (218,750 bytes) and whole-symbol
  packetized repair accounting (219,650 bytes);
- transport overhead and rate-window budgeting without double deduction;
- foundational whole-block semantic admission ordered P0/P1/P2, including
  FEC cost, deadline/age drops, and block/byte limits;
- reorder/duplicate-aware block assembly, recovered-symbol accounting,
  bounded buffering, one-shot completion, and actual expired-state cleanup;
- monotonic P0, P0+P1, and complete frame availability tracking.

Fresh verification: the clang-cl build completed without warnings and all
38 discovered tests passed.

Still pending before Checkpoint A is complete:

- rate hysteresis and capacity-step tests;
- additional corrupt/cross-frame/metadata/memory-bound receiver cases.

(The deterministic full model pipeline and loss/reorder golden summaries were
completed as Task 6; see the section above.)

No WebRTC source file has been changed yet. Native repair generation,
PeerConnection, ICE/DTLS/SRTP, TWCC/GoogCC, pacer, RTP/RTCP, and real localhost
network loopback remain pending behind the planned WebRTC seam gate.

## 2026-09-27 — Milestone A, first vertical slice

Implemented with test-first cycles:

- clang-cl + Chromium libc++ build/test contract;
- deterministic 2,000,000-byte simulated frame generation;
- fixed P0/P1/P2 partitioning at 250,000/750,000/1,000,000 bytes;
- rejection of malformed frame sizes before transport;
- 1,150-byte source symbols and at most 32 source symbols per block;
- fixed protection policy: P0 50%, P1 12.5%, P2 0%, with tail rounding up;
- an in-process full-frame accounting test.

Not implemented yet:

- wire serialization and packet integrity;
- actual FEC encode/decode and loss recovery;
- admission/budget control;
- receiver assembly and partial usability;
- WebRTC transport seam and real localhost PeerConnection;
- sender, receiver, signaling runtime processes.

This distinction is intentional: an in-process pipeline test is not reported as a network or WebRTC loopback result.

## 2026-09-27 — Task 11 batch 1: transport feedback foundation

- Fresh runtime baseline before Task 11: 68/68 tests passed with zero skips.
- RED: `volumetric_transport_feedback_test` failed to compile because the
  feedback store, rate update, observation snapshot, and bitrate sampler did
  not exist.
- GREEN: added a WebRTC-independent, thread-safe feedback store that keeps
  missing RTCStats values unavailable instead of fabricating zeroes.
- Added actual-sent-bitrate derivation from monotonic sample time and outbound
  byte-counter deltas, including counter rollback, duplicate timestamp, and
  overflow handling.
- A clean focused build and 11/11 related tests passed after the tests exposed
  and removed three partial-designated-initializer warnings.
- Corrected the Task 11 written specification from binary 2 MiB to the
  project's existing decimal 2 MB contract: 2,000,000 bytes split as
  P0=250,000, P1=750,000, and P2=1,000,000 bytes.

No WebRTC source file was changed in this batch.

## 2026-09-27 — Task 11 batch 2: simulated access units and rate wiring

- RED: the new access-unit tests failed because no codec-boundary envelope or
  bounded queue existed.
- GREEN: added a 28-byte network-order OCTREE-SIM access-unit envelope with
  frame identity, RTP timestamp, payload length, and FNV-1a checksum.
- The parser rejects truncation, bad magic/version, length mismatch, checksum
  corruption, and payloads above the project's 2,000,000-byte frame contract.
- Added a thread-safe two-entry latest-frame queue with exact RTP timestamp
  matching and observable oldest-frame eviction; 6/6 focused tests pass.
- RED: the WebRTC-rate integration test exposed two incompatible
  `RateUpdateSink` definitions and could not construct the feedback store.
- GREEN: moved the sink contract into the WebRTC-free transport boundary and
  changed OCTREE-SIM `SetRates` to publish one timestamped
  `EncoderRateUpdate` per callback.
- The focused WebRTC rate/factory suite passes 9/9. The 8 Mbps to 0.8 Mbps
  callback change reduces a 33 ms budget to 3,300 bytes without consulting
  RTCStats or an application timer.

The build emitted the pinned WebRTC headers' existing nullability and unused
parameter diagnostics; no new application warning was introduced. No file
under `extern/webrtc/src` was changed in this batch.
