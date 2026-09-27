# Task 7–10 hardening review

## Acceptance verdict after remediation

| Task | Verdict | Evidence | Remaining boundary |
|---|---|---|---|
| 7 | Accepted | The seam study identifies the correct Generic RTP, ULPFEC and post-FEC assembly locations and freezes a narrow patch boundary. | Keep file/line claims synchronized when the pinned WebRTC revision changes. |
| 8 | Accepted as an upstream capability, not as application wiring | Four focused upstream tests pass for explicit FEC block flush, per-block protection rates, incomplete-frame behavior and post-FEC payload metadata. | `FlushBlock` and the receive observer are still internal to libwebrtc. Tasks 12/13 must connect them to the PeerConnection media path before E2E FEC can be claimed. |
| 9 | Accepted for factory/rate-control foundation | Patched archive links; a factory-created encoder now forwards `SetRates` through a shared, lifetime-safe sink. | Actual GoogCC/TWCC rate evolution requires a sending track and belongs to Task 11. |
| 10 | Accepted for the current localhost lifecycle scope | Real host/UDP candidates, ICE and DTLS complete without skip; asynchronous SDP failure and ordered shutdown are tested. | SRTP payload confidentiality and multi-process signaling are later acceptance items, not proven by this single-process lifecycle test. |

## Root causes found

1. The Windows embedding omitted both `WinsockInitializer` and
   `InitializeSSL`. The earlier test therefore gathered only TCP-active
   candidates and incorrectly attributed the result to the machine firewall.
2. The session was uniquely owned but also used as a WebRTC ref-counted SDP
   observer with no-op `AddRef/Release`. Pending callbacks could outlive the
   assumptions of the owner.
3. Remote SDP was marked committed immediately after starting the asynchronous
   operation. Buffered candidates could therefore be drained against an SDP
   which later failed.
4. Local SDP was signaled before `SetLocalDescription` completed, and all SDP
   errors were discarded.
5. `kConnected` was published on ICE connected/completed, before the
   standardized PeerConnection state proved DTLS completion.
6. The production encoder factory always constructed `OctreeSimEncoder()` with
   a null sink; the previous tests covered only direct encoder construction.

## Remediation design

- A process-lifetime runtime guard initializes Winsock and SSL before any
  WebRTC thread/socket is created and reports initialization failure from
  `Start()`.
- Separate ref-counted observers own all asynchronous Create/Set SDP work.
  Their shared state survives the initiating stack frame without retaining the
  `PeerConnectionSession` object itself.
- Local SDP is released to signaling only after local commit. Remote candidate
  draining occurs only after remote commit. Parse/add failures and asynchronous
  SDP failures enter a terminal failed state and optionally call `on_error`.
- The application state machine treats `kFailed` as terminal and suppresses
  duplicate state callbacks. Only standardized PeerConnection `kConnected`
  publishes application `kConnected` and sets `dtls_connected=true`.
- Rate feedback uses shared ownership across callbacks, factory and encoders;
  this avoids a raw sink pointer becoming dangling while WebRTC owns encoder
  instances asynchronously.

## Tests added or strengthened

- The localhost ICE/DTLS test cannot skip. It prints every gathered candidate
  when UDP is absent and checks every `AddRemoteIceCandidate` return value.
- A valid SDP deliberately applied with the wrong type/state proves that an
  asynchronous remote-description failure becomes `kFailed` and contains a
  useful error message.
- A factory-level rate test proves that `SetRates` on a factory-created encoder
  reaches the injected sink.
- The full runtime preset passes 68/68 tests with zero skips. The four focused
  upstream WebRTC tests pass independently.

## Claims deliberately not made

- Task 8 does not yet prove PeerConnection-level semantic FEC transmission or
  recovery.
- Task 9 does not yet prove live TWCC/GoogCC callbacks because no simulated
  volumetric sending track feeds the encoder in this milestone.
- Task 10 proves ICE and DTLS lifecycle, not SRTP ciphertext inspection, RTP
  media flow, multi-process signaling or long-duration soak behavior.
