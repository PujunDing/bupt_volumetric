# Task 12 Native FEC Send Path — Implementation Plan

## Research findings (verified against pinned WebRTC)

The send path is:

```
OctreeSimEncoder::Encode -> EncodedImage
  -> RtpVideoSender::OnEncodedImage            (call/rtp_video_sender.cc:551)
    -> fec_controller_->UpdateWithEncodedData  (size/type only)
    -> RTPSenderVideo::SendEncodedImage        (packetizer -> RTP packets, marker on last)
      -> RtpSenderEgress::SendPacket           (rtp_sender_egress.cc:185)
        -> apply pending_fec_params_ via SetProtectionParameters
        -> fec_generator_->AddPacketAndGenerateFec(packet)
  -> RtpSenderEgress::FetchFecPackets          (rtp_sender_egress.cc:403)
    -> fec_generator_->GetFecPackets()          -> repair RED packets
```

Key facts:

1. **Marker = block boundary.** `RtpPacketizerGeneric` sets the RTP marker on the
   last packet of each `EncodedImage`; `UlpfecGenerator::AddPacketAndGenerateFec`
   (Task 8 seam) flushes on the marker. So one `EncodedImage` per semantic block
   already yields one FEC block per semantic block.
2. **fec_rate is per-stream today.** `FecControllerDefault` computes one
   `FecProtectionParams.fec_rate` and applies it via
   `RtpVideoSender::ProtectionRequest` -> `RtpSenderEgress::SetFecProtectionParameters`
   -> `pending_fec_params_` (applied on the next packet). Semantic FEC needs the
   encoder to set this **per block**.
3. **Repair never bypasses the pacer/SRTP.** `GetFecPackets()` returns RED packets
   that go through the same egress; no side socket exists and none will be added.
4. **`FecControllerOverride` is a bool only** (`SetFecAllowed`), so it cannot carry
   a per-block fec_rate; the seam below is required.

## Design

### A. Per-block access units (app side, no WebRTC)

`SubmitSimulatedFrame` already partitions/plans/admits. Change it to queue **one
access unit per admitted block** instead of one per frame, and add a send-side
`fec_rate` (Q8) to the in-memory unit. The serialized 28-byte wire envelope stays
unchanged (fec_rate is send-side only; the receiver reads block structure from the
48-byte OCTREE-SIM header in Task 13).

### B. Encoder emits one EncodedImage per block + a fec hint

`OctreeSimEncoder::Encode` pops one block unit, emits one `EncodedImage` carrying
that block's source bytes, and attaches the block's `fec_rate` to the
`CodecSpecificInfo` it passes to `OnEncodedImage`.

### C. Minimal WebRTC seam: per-block fec_rate (frozen Task 8 seam extended)

- `CodecSpecificInfo` gains `std::optional<std::uint16_t> fec_rate`.
- `RtpVideoSender::OnEncodedImage`, before `SendEncodedImage`, reads the hint and,
  when present, calls a new `ModuleRtpRtcpImpl2::SetSemanticFecProtectionParams`
  with `p.fec_rate = *hint`, `p.max_fec_frames = 1`, `p.fec_mask_type = kFecMaskRandom`.
  P2 sets `fec_rate = 0` explicitly (no repair).
- **Design refinement (found while integrating):** routing the hint through the
  existing `ProtectionRequest`/`pending_fec_params_` slot is wrong — the
  FecController's zero-loss dynamic rate (0) overwrites it before the pacer sends
  the block, and a single slot is overwritten when the encoder runs ahead of the
  pacer. The fix is a per-frame semantic FIFO in `RtpSenderEgress` that takes
  precedence over the dynamic rate and sticks for the whole block.
- **Second integration fix:** `ShouldDisableRedAndUlpfec` disables ULPFEC for a
  generic payload + NACK, so the app enables `WebRTC-GenericPictureId` via
  `DeprecatedGlobalFieldTrials::Set`.
- No pacer/GoogCC/egress algorithm changes; the marker still drives the flush.

### D. Accounting ledger (app side)

A `FecSendLedger` counts, from the send path: `source_packets/source_bytes`
(planned source symbols), `repair_packets/repair_bytes` (derived from the block's
fec_rate via `ForwardErrorCorrection::NumFecPackets` semantics — the same formula
the FEC generator uses), and `header_bytes` (48 per source symbol) + `red_overhead`
(1 per repair packet). Padding/RTCP stay out of the ledger and are the only allowed
discrepancy versus egress counters.

## Increments (TDD)

- [x] **T12.1** — Add `fec_rate` + `block_id` to the in-memory `SimulatedAccessUnit`
  (wire envelope unchanged); extend the queue + serialization tests.
- [x] **T12.2** — `SubmitSimulatedFrame` queues one unit per admitted block; add a
  focused test that a 2 MB frame yields 56 units (P0=7, P1=21, P2=28) when
  unconstrained.
- [x] **T12.3** — Encoder emits one `EncodedImage` per block and attaches the
  block's `fec_rate` to `CodecSpecificInfo`; add a media test asserting the hint.
- [x] **T12.4** — WebRTC seam: `CodecSpecificInfo::fec_rate` + `RtpVideoSender`
  per-block semantic override (via `SetSemanticFecProtectionParams`). GREEN:
  6-file patch + app field trial. Upstream `rtp_video_sender_unittest` assertion
  is still a follow-up (the seam method changed from `SetFecProtectionParams`
  to `SetSemanticFecProtectionParams`).
- [x] **T12.5** — `FecSendLedger` accounting (source vs repair vs header/RED) with
  full-block (32+16, 32+4) and tail-block (<32 source) golden tests.
- [x] **T12.6** — Rebuild `webrtc.lib` (321,950,212 bytes), run the full runtime
  suite (100/100) + pure-algorithm suite (58/58), record provenance. End-to-end
  repair generation verified via temporary diagnostics (then removed), and a
  persistent `fec_packets_sent > 0` egress-alignment assertion added to
  `localhost_media_flow_test` via the new provisional outbound FEC stat.

## Acceptance

Generated repair counts equal the protection policy (P0 ceil(0.5K), P1
ceil(0.125K), P2 0); the ledger aligns with egress counters up to documented
padding/RTCP.

Status: **verified on the send path, including a persistent egress-alignment
assertion.** Runtime repair generation matches the policy (diagnostics: P0
`fec_rate=128` → 17/34 and 7/13, P1 `fec_rate=32` → 2/13 and 1/6), repair is
fetched/sent through the pacer/SRTP and observed at the receiver's
`UlpfecReceiver`. A non-standard provisional `fec_packets_sent/fec_bytes_sent`
on `RTCOutboundRtpStreamStats` (mapped in `rtc_stats_mapper.cc`) makes the
send-side egress counter persistently observable; `localhost_media_flow_test`
asserts it is `> 0`. The model ledger (`FecWireAccounting`) matches the policy
at the 1,150-byte symbol level. One honest caveat remains: the receive-side
`fec_packets_received` stat is FlexFEC-only, so ULPFEC repair reception is not
yet a persistent stat (deferred to Task 13).
