# Architecture Overview

Scaffold contract: the sender performs deterministic generation, semantic partitioning, admission, packetization, then hands packets to native WebRTC pacing and congestion control. The receiver observes post-FEC packets before whole-video-frame assembly, builds semantic blocks, and emits measurements without rendering.

The application controls semantic value; WebRTC controls transport rate. These responsibilities must never be merged into competing congestion controllers.
