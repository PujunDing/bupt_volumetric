# WebRTC Patch Boundary

Frozen after Task 7 spike (decision: GO). The patch is limited to narrow
capabilities on surfaces WebRTC already uses internally. It must **not** rewrite
GoogCC, pacer, ICE, DTLS, SRTP, or PeerConnection, and it must **not** reimplement
the FEC algorithm (reuse `ForwardErrorCorrection::EncodeFec`).

## Capability 1 — Sender: explicit FEC block lifecycle

- `UlpfecGenerator` gains an explicit begin/add/end (or `flush`) path independent of
  `packet.Marker()`. Stock marker-flush behavior remains the default when the
  extension is not used.
- Files: `modules/rtp_rtcp/source/ulpfec_generator.{h,cc}`,
  `modules/rtp_rtcp/source/video_fec_generator.h`.

## Capability 2 — Sender: per-block protection parameters

- Thread a per-block `fec_rate` (and, where it already exists, the
  `num_important_packets`/`use_unequal_protection` knobs) through to
  `ForwardErrorCorrection::EncodeFec`. No business P0/P1/P2 type enters WebRTC; it
  only receives generic block metadata and protection numbers.
- Files: `modules/include/module_fec_types.h` (or a narrow parallel struct),
  `ulpfec_generator.cc`.

## Capability 3 — Receiver: post-FEC, pre-assembly per-packet observer

- A per-packet callback invoked from `RtpVideoStreamReceiver2::OnReceivedPayloadData`
  (thread: `packet_sequence_checker_`) carrying `codec_payload`, `recovered`, sequence
  number, timestamp, and receive time. FEC repair packets never enter the callback as
  business packets.
- Files: `video/rtp_video_stream_receiver2.{h,cc}`.

## Capability 4 — Read-only pacer observation (Task 11)

- Exposes the existing internal `Call::Stats::pacer_delay_ms` as
  `RTCIceCandidatePairStats::pacer_delay` (seconds, selected/best pair only). This
  is observational only; no pacer, GoogCC, task-queue, or send-scheduling behavior
  changes.
- Files: `api/stats/rtcstats_objects.h`, `stats/rtcstats_objects.cc`,
  `pc/rtc_stats_collector.cc` (+ `pc/rtc_stats_collector_unittest.cc`).

## Constraints

- Default behavior is unchanged when the semantic extension is not enabled.
- P0/P1/P2 business logic stays in `bupt_volumetric`; upstream sees only generic
  block metadata, importance count, and protection params.
- Only the directly affected `BUILD.gn` files are touched.
- Every WebRTC build in this checkout must set `DEPOT_TOOLS_WIN_TOOLCHAIN=0`
  (local MSVC 14.29.30133).
- `obj/webrtc.lib` (321,831,232 bytes, mtime 2026-06-02) is the baseline that must
  not regress silently; Task 9 adds the provenance check before linking it.
