# Congestion Control Boundary

GoogCC/TWCC owns the transport target and WebRTC pacer owns send timing. The
semantic controller consumes a snapshot and only admits or drops whole semantic
blocks. It must not implement a second AIMD loop or bypass pacing.

## Control and observation paths

- **High-frequency control path** (no timer): WebRTC rate controller →
  `OctreeSimEncoder::SetRates` → `RateUpdateSink::OnRateUpdate` →
  `TransportFeedbackStore`. One `EncoderRateUpdate{target_bitrate_bps,
  target_framerate_fps, observed_at_ms}` is published per callback.
- **Low-frequency observation path** (no hidden polling thread):
  `PeerConnectionSession::CollectTransportStats()` issues one asynchronous
  `GetStats` whose `RTCStatsCollectorCallback` maps the report and merges it into
  the same store. Applications/tests choose their own sampling cadence.

## RTCStats → domain mapping

`RtcStatsMapper::MapRtcStats` converts a `webrtc::RTCStatsReport` into a nullable
`RtcTransportObservation`. Durations exposed in seconds are normalized to
milliseconds at this boundary; missing/non-finite/negative values stay absent.

| Source field | Output field | Unit | Notes |
|---|---|---|---|
| `RTCTransportStats.selected_candidate_pair_id` | — | — | selects exactly one pair |
| `RTCIceCandidatePairStats.current_round_trip_time` | `selected_pair_rtt_ms` | ms | seconds → ms |
| `RTCIceCandidatePairStats.available_outgoing_bitrate` | `available_outgoing_bitrate_bps` | bps | |
| `RTCIceCandidatePairStats.pacer_delay` | `pacer_delay_ms` | ms | read-only patch, seconds → ms |
| `RTCOutboundRtpStreamStats.bytes_sent` (video) | `outbound_bytes` | bytes | summed over video streams |
| `RTCOutboundRtpStreamStats.packets_sent` (video) | `outbound_packets` | packets | summed |
| `RTCOutboundRtpStreamStats.retransmitted_*` (video) | `retransmitted_*` | bytes/packets | summed |
| `RTCOutboundRtpStreamStats.total_packet_send_delay` | `total_packet_send_delay_ms` | ms | summed, seconds → ms |
| `RTCRemoteInboundRtpStreamStats.fraction_lost` | `loss_fraction` | ratio | clamped to [0,1] when finite |
| `RTCRemoteInboundRtpStreamStats.jitter` | `jitter_ms` | ms | mean, seconds → ms |
| `RTCRemoteInboundRtpStreamStats.round_trip_time` | `remote_round_trip_time_ms` | ms | mean, seconds → ms |

`actual_sent_bitrate_bps` is **derived** from outbound byte-counter deltas over
monotonic sample time; counter regression or non-increasing time resets the
baseline and yields no value.

## Semantic admission consumption

`capacity_step_test` proves the store→budget→admission chain: a downward rate
step drops P2 before P1 before P0, and an upward step re-admits more total bytes.
Admission is whole-block and priority-ordered; it never bypasses the pacer.

## Provenance

- `pacer_delay_ms` — **observed** (WebRTC stats field).
- `actual_sent_bitrate_bps` — **derived** (byte-counter deltas).
- `estimated_transport_overhead_bytes` — **estimated**.
