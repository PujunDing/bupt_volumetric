# WebRTC Patch Set

Vendored patches against the pinned WebRTC commit
`8a3d684776938a4614f892f81859136ff4855ea3`. Every patch is read-only with
respect to congestion-control and scheduling behavior; none rewrites GoogCC,
pacer, ICE, DTLS, SRTP, or PeerConnection logic.

## 1. Task 8 — semantic FEC block seams

Three narrow capabilities on surfaces WebRTC already exposes internally:

- `VideoFecGenerator::FlushBlock()` / `UlpfecGenerator::FlushBlock()` — explicit
  FEC block flush independent of the RTP marker; per-block `fec_rate` via the
  existing `SetProtectionParameters`.
- `RtpVideoStreamReceiver2::PostFecSourcePayloadObserver` — a post-FEC,
  pre-assembly per-packet callback (payload / sequence / timestamp / recovered /
  receive time).

Files: `modules/rtp_rtcp/source/video_fec_generator.h`,
`modules/rtp_rtcp/source/ulpfec_generator.{h,cc}`,
`video/rtp_video_stream_receiver2.{h,cc}` (+ two unittest files).

## 2. Task 11 — read-only pacer observation

Exposes the existing internal `Call::Stats::pacer_delay_ms` as a
`RTCIceCandidatePairStats::pacer_delay` field (seconds, matching other RTCStats
duration fields), only on the selected/best candidate pair.

- `api/stats/rtcstats_objects.h` — `std::optional<double> pacer_delay;`
- `stats/rtcstats_objects.cc` — `AttributeInit("pacerDelay", &pacer_delay)`
- `pc/rtc_stats_collector.cc` — copies `call_stats.pacer_delay_ms` into the best
  pair only when `>= 0`
- `pc/rtc_stats_collector_unittest.cc` — upstream regression test

**Invariant:** no pacer, GoogCC, task-queue, or send-scheduling behavior is
changed. The field is observational only; the application converts seconds to
milliseconds at its own boundary.

## Build requirements

- Set `DEPOT_TOOLS_WIN_TOOLCHAIN=0` (local MSVC 14.29.30133) for every
  `gn`/`ninja` invocation in `extern/webrtc/src`.
- Upstream test: `ninja -C out/Default rtc_stats_collector_unittests`
- Archive: `ninja -C out/Default webrtc` (rebuilt from patched sources).
- `bupt_volumetric_import_webrtc()` provenance-checks that `obj/webrtc.lib` is
  newer than every Task 8 patched source file before linking.
