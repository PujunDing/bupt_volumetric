# WebRTC Seam Spike — Facts Verification (Task 7, Phase A)

Status: **facts recorded + minimal failing tests written and executed (Phase B done).**
Checkout: `D:\BUPT-XR\extern\webrtc\src` (pinned `8a3d684776938a4614f892f81859136ff4855ea3`).
Upstream files modified so far: two **test** files only (no production code):
`modules/rtp_rtcp/source/ulpfec_generator_unittest.cc`,
`video/rtp_video_stream_receiver2_unittest.cc`.

This document records the concrete seam facts required by Task 7 acceptance:
exact class, method, file:line, thread/sequence checker, lifecycle, and test
target — not a concept diagram. The two failing tests (Phase B) and the final
go/no-go call are appended after the spike build runs.

---

## Fact 1 — A custom factory name maps to `kVideoCodecGeneric` without touching the global enum

- `VideoEncoderFactory::GetSupportedFormats()` returns `std::vector<SdpVideoFormat>`
  (`api/video_codecs/video_encoder_factory.h:75`). Each `SdpVideoFormat` carries only
  `name` + `parameters` (`api/video_codecs/sdp_video_format.h:69`); it has **no**
  codec-type field.
- `PayloadStringToCodecType()` (`api/video_codecs/video_codec.cc:163-176`) returns
  `kVideoCodecGeneric` for **any** unrecognized SDP name (the final `return
  kVideoCodecGeneric;` at line 175). So `"OCTREE-SIM"` → `kVideoCodecGeneric`.
- `CodecTypeToPayloadString(kVideoCodecGeneric)` returns the literal `"Generic"`
  (`video_codec.cc:155-156`), not `"OCTREE-SIM"`. Round-trip is therefore asymmetric:
  name→type is lossy but type→name is fixed. This only matters where code re-derives a
  display/log name from the type (e.g. `VideoCodec::ToString()` at `video_codec.cc:83`).

**Conclusion:** the Task 9 approach holds — advertise `SdpVideoFormat{"OCTREE-SIM", {}}`
from a custom factory and let WebRTC treat it as generic via the fallback. No global
codec-enum extension is needed. Residual to confirm in the failing tests / Task 9:
the SDP negotiation must use the factory name (not `CodecTypeToPayloadString`) for the
wire format, and the sender/receiver must select the generic packetizer/depacketizer.

---

## Fact 2 — The generic packetizer never rewrites payload bytes

- `RtpPacketizerGeneric::NextPacket()` (`modules/rtp_rtcp/source/rtp_format_video_generic.cc:56-86`)
  copies source bytes verbatim (`memcpy(out_ptr + header_size_, ...)`, line 73) and only
  prepends a 1-byte generic header (3 bytes with the extended picture-id path, lines 88-103).
- It sets the RTP marker on the **last** packet: `packet->SetMarker(remaining_payload_.empty())`
  (`rtp_format_video_generic.cc:84`). Marker = whole-frame boundary.
- The depacketizer mirrors this: `VideoRtpDepacketizerGeneric::Parse()`
  (`modules/rtp_rtcp/source/video_rtp_depacketizer_generic.cc:38-72`) strips the 1/3-byte
  header and returns `video_payload = rtp_payload.Slice(offset, ...)` (line 69) untouched.

**Conclusion:** the 48-byte OCTREE-SIM wire header embedded at the start of our payload
survives generic packetization byte-for-byte. Payload is split, never rewritten.

---

## Fact 3 — ULPFEC flushes on RTP marker and hardcodes equal protection

- Flush trigger: `UlpfecGenerator::AddPacketAndGenerateFec()`
  (`modules/rtp_rtcp/source/ulpfec_generator.cc:113-170`) reads
  `const bool complete_frame = packet.Marker();` (line 134) and only encodes when
  `complete_frame && (num_protected_frames_ >= params.max_fec_frames || (ExcessOverheadBelowMax() && MinimumMediaPacketsReached()))`
  (lines 157-159).
- Equal protection is hardcoded:
  `constexpr int kNumImportantPackets = 0; constexpr bool kUseUnequalProtection = false;`
  (`ulpfec_generator.cc:161-162`) — these are **not** exposed through `FecProtectionParams`.
- `FecProtectionParams` (`modules/include/module_fec_types.h:26-30`) only carries
  `{fec_rate, max_fec_frames, fec_mask_type}` — no importance count / UEP switch.
- Max media packets per block: `kUlpfecMaxMediaPackets = 48`
  (`modules/rtp_rtcp/source/forward_error_correction_internal.h:25`), enforced at
  `ulpfec_generator.cc:135`. Our ≤32 source symbols/block fit comfortably.
- The UEP capability **already exists one layer down**:
  `ForwardErrorCorrection::EncodeFec(media_packets, protection_factor,
  num_important_packets, use_unequal_protection, fec_mask_type, fec_packets)`
  (`modules/rtp_rtcp/source/forward_error_correction.h:202-207`), with the important
  packets expected at the **start** of the media list (comment at lines 175-187).

**Conclusion:** stock ULPFEC cannot do per-semantic-block protection. The seam is
(1) an explicit FEC block begin/end/flush independent of the RTP marker, and
(2) per-block protection params threaded to `EncodeFec` (reusing its existing
`num_important_packets`/`use_unequal_protection` rather than writing a new FEC algorithm).
This matches plan risk B and Task 8.

---

## Fact 4 — `packet.recovered()` exists, but there is no post-FEC per-packet observer

- `RtpPacketReceived::recovered()` / `set_recovered()`
  (`modules/rtp_rtcp/source/rtp_packet_received.h:56-58`, member `recovered_` at line 78) —
  "recovered via RTX or FEC".
- ULPFEC-recovered packets re-enter through
  `RtpVideoStreamReceiver2::OnRecoveredPacket()`
  (`video/rtp_video_stream_receiver2.cc:753-761`) → `ReceivePacket()` with
  `packet.recovered() == true`.
- Regular packets enter through `RtpVideoStreamReceiver2::OnRtpPacket()`
  (`rtp_video_stream_receiver2.cc:765-788`) → `ReceivePacket()`.
- Both paths converge at
  `RtpVideoStreamReceiver2::OnReceivedPayloadData(CopyOnWriteBuffer codec_payload,
  const RtpPacketReceived& rtp_packet, const RTPVideoHeader& video, int times_nacked)`
  (`rtp_video_stream_receiver2.cc:552-751`). At this point both the raw `codec_payload`
  and `rtp_packet.recovered()` are available, but the packet is pushed straight into
  `packet_buffer_.InsertPacket(...)` (line 748) with **no per-packet observer hook**.
- Frame assembly is marker-driven and whole-frame only: `OnInsertedPacket()`
  (`rtp_video_stream_receiver2.cc:823-925`) calls `AssembleFrame(payloads)` only when
  `packet->is_last_packet_in_frame()` (line 881), then `OnAssembledFrame()`
  (line 927) emits one complete `RtpFrameObject`.
- `RtpVideoStreamReceiver2` already implements `RecoveredPacketReceiver`
  (`OnRecoveredPacket`, line 753) — proof that a narrow post-FEC callback seam is the
  same shape WebRTC itself already uses.

**Conclusion:** an unrecovered P2 packet keeps the whole marker-terminated frame from
being emitted, so P0/P1 would be blocked — exactly plan risk A. The seam is a per-packet
observer invoked from `OnReceivedPayloadData` (or immediately before
`packet_buffer_.InsertPacket`) that carries `codec_payload`, `recovered`, sequence
number, and receive time. Thread: `packet_sequence_checker_` (both `OnRtpPacket` and
`OnRecoveredPacket` assert `RTC_DCHECK_RUN_ON(&packet_sequence_checker_)`).

---

## Fact 5 — Pacer observability is already read-only and clock-free

- `RtpPacketPacer` (`modules/pacing/rtp_packet_pacer.h:24-68`) exposes
  `OldestPacketWaitTime()`, `QueueSizeData()`, `FirstSentPacketTime()`,
  `ExpectedQueueTime()`, `SetQueueTimeLimit()` — all read-only observability, no
  wall-clock in the app-facing interface.
- `TaskQueuePacedSender` (`modules/pacing/task_queue_paced_sender.h:39-182`) is the
  production impl on a dedicated `task_queue_`. Entry: `EnqueuePackets()`
  (line 70). Exit: `PacingController::PacketSender::SendPacket()` (owned by
  `pacing_controller_`, line 154). It also maintains a protected `Stats` struct and
  `OnStatsUpdated(const Stats&)` (lines 118-129).
- The actual wire send / sequence-number assignment / egress counters live in
  `modules/rtp_rtcp/source/rtp_sender_egress.{h,cc}` — this is the post-pacer point
  named in the Task 8 candidate list.

**Conclusion:** pacer queue depth and expected/wait time are already observable through
`RtpPacketPacer`; no pacer patch is needed for the "before/after pacer" evidence. The
egress (post-pacer) is where per-block FEC flush and send counters will be observed.

---

## Preliminary seam assessment (to be confirmed by the failing tests)

Three narrow capabilities satisfy the whole feature, each touching a surface WebRTC
already exposes or uses internally:

1. **Sender — explicit FEC block lifecycle.** `UlpfecGenerator` gains an explicit
   begin/add/end (or `flush`) path independent of `packet.Marker()`, plus per-block
   protection params. Reuse `ForwardErrorCorrection::EncodeFec`'s existing
   `num_important_packets`/`use_unequal_protection`; do **not** reimplement FEC.
2. **Sender — per-block protection params** through `FecProtectionParams` (or a narrow
   parallel struct), keeping stock `{fec_rate, max_fec_frames, fec_mask_type}` behavior
   as the default.
3. **Receiver — post-FEC, pre-assembly per-packet observer** invoked from
   `OnReceivedPayloadData` carrying `codec_payload`, `recovered`, sequence number,
   timestamp, receive time.

Expected patch surface (freeze after Phase B): `ulpfec_generator.{h,cc}`,
`video_fec_generator.h`, `forward_error_correction.*` (only if UEP param plumbing is
required), `rtp_video_stream_receiver2.{h,cc}`, plus the two unit test files and the
directly affected `BUILD.gn` files.

## Phase B build targets (confirmed)

- `rtp_rtcp_unittests` — the `ulpfec_generator_unittest.cc` test is folded into the
  combined **`modules_unittests.exe`** (`phony/modules/rtp_rtcp/rtp_rtcp_unittests.linkdeps`
  → output `modules_unittests.exe`).
- `rtp_video_stream_receiver2_unittest_bin` → `video_rtp_video_stream_receiver2_unittest_bin.exe`.

Build command that works in this vendored checkout (see the environment note below):

```powershell
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"   # use local MSVC 14.29.30133
D:\BUPT-XR\extern\webrtc\src\third_party\ninja\ninja.exe `
  -C D:\BUPT-XR\extern\webrtc\src\out\Default `
  modules_unittests.exe rtp_video_stream_receiver2_unittest_bin
```

## Phase B results (executed)

1. **Sender red test** `UlpfecGeneratorTest.CannotFlushASemanticBlockWithoutMarker`
   (in `modules_unittests.exe`): **FAILED** as intended —
   `ulpfec_generator_.GetFecPackets().empty()` was `Actual: true, Expected: false`.
   This proves stock ULPFEC cannot flush a semantic block without the RTP marker.

2. **Receiver documentation test**
   `RtpVideoStreamReceiver2Test.MissingMiddlePacketBlocksWholeFrameAssembly`
   (in `video_rtp_video_stream_receiver2_unittest_bin.exe`): **PASSED** — with a
   missing middle (sequence-2) packet, `DoOnCompleteFrame` is never called, so the
   whole frame (P0/P1 included) stays buffered. This documents the coupling Task 8
   must break with a post-FEC, pre-assembly per-packet observer.

## Environment note (recorded for Task 8/9)

- The depot_tools win_toolchain (`third_party/depot_tools/win_toolchain/vs_files`) is
  **empty** and `build/win_toolchain.json` is absent, so a plain `ninja` triggers a
  failing `gn` regeneration. The checkout was originally built with
  `DEPOT_TOOLS_WIN_TOOLCHAIN=0` against the locally installed
  `C:\Program Files (x86)\Microsoft Visual Studio\2019\Community`
  (MSVC `14.29.30133`). Every future WebRTC `gn`/`ninja` invocation must set
  `DEPOT_TOOLS_WIN_TOOLCHAIN=0`.
- `obj/webrtc.lib` remains unchanged at **321,831,232 bytes** (mtime 2026-06-02); the
  spike rebuild touched only the unittest object files and the two test executables,
  not the baseline library.

## Go / no-go decision: **GO**

Three narrow capabilities satisfy the whole feature, each on a surface WebRTC already
uses internally, and none requires reimplementing FEC or extending the global codec
enum:

1. **Sender block lifecycle** — explicit begin/add/end/flush in `UlpfecGenerator`,
   independent of `packet.Marker()`.
2. **Sender per-block protection** — thread `fec_rate` (and optionally the existing
   `num_important_packets`/`use_unequal_protection`) through to
   `ForwardErrorCorrection::EncodeFec`.
3. **Receiver post-FEC observer** — a per-packet callback from
   `RtpVideoStreamReceiver2::OnReceivedPayloadData` carrying `codec_payload`,
   `recovered`, sequence number, timestamp, and receive time.

This satisfies Task 7's go criterion ("可用不超过三个窄接口解决") and unblocks Task 8.
