# BUPT Volumetric — 语义分级 FEC 仿真原型

一个用于研究「模拟八叉树点云」语义分级传输（P0/P1/P2 差异化前向纠错）的本地仿真系统。它**不做渲染、不产生图像**——接收端只记录可用性、FEC 恢复、拥塞、队列和延迟等指标，用于回答「语义分块 FEC 相比统一 FEC / 无 FEC 是否更好」这一研究问题。

> 本项目是**仿真/研究原型**：点云数据是确定性生成的 2,000,000 字节模拟帧，不是真实 G-PCC 编码；媒体走真实 WebRTC（ICE/DTLS/SRTP/RTP/RTCP/TWCC/GoogCC/pacer/ULPFEC），但**不承诺跨公网/NAT/Wi-Fi 的性能结论**（当前结论只适用于 `topology=localhost-host-candidate`）。

---

## 1. 系统长什么样（三层结构）

```
bupt_volumetric_sender.exe           bupt_volumetric_receiver.exe
  模拟帧 -> 语义分块 -> 准入 ->          UDP/SRTP -> RED/ULPFEC 恢复 ->
  OCTREE-SIM encoder -> RTP packetizer  OCTREE-SIM depacketizer ->
  -> ULPFEC -> pacer -> SRTP/UDP        语义块组装 -> 可用性 -> 指标
        |                                    |
        +------- SDP / ICE / 控制 ----------+
                        |
        bupt_volumetric_signaling_server.exe   (TCP 信令路由)
```

- **应用核心层**（`model/protection/receiver/simulation/transport`，不依赖 WebRTC）：线格式、语义分块、预算、准入、接收端组装、指标。
- **WebRTC 适配层**（`rtc/`）：OCTREE-SIM codec factory、`SetRates`→预算、FEC 接缝、RTCStats 映射。
- **进程层**（`apps/` + `signaling/`）：信令、控制 DataChannel、三个可运行进程。

固定数据模型：每帧 2,000,000 B；P0=250,000 B（50% FEC）、P1=750,000 B（12.5% FEC）、P2=1,000,000 B（0% FEC）；source symbol 目标 1,150 B、每块 ≤32 个；RTP 时钟 90 kHz。

---

## 2. 怎么构建

依赖：Windows x64 + clang-cl + Ninja + 已编译的 `extern/webrtc/src/out/Default/obj/webrtc.lib`（补丁见 `webrtc_patches/manifest.json`）。

```powershell
# 运行时预设（链接 patched WebRTC，含三进程 + 全部测试）
cmake --preset windows-clang-runtime -S D:\BUPT-XR\bupt_volumetric
cmake --build --preset windows-clang-runtime

# 纯算法预设（不链接 WebRTC，只跑算法测试）
cmake --preset windows-clang-release -S D:\BUPT-XR\bupt_volumetric
cmake --build --preset windows-clang-release
```

产物：
- `out/bupt_volumetric/clang-runtime/bupt_volumetric_signaling_server.exe`
- `out/bupt_volumetric/clang-runtime/bupt_volumetric_sender.exe`
- `out/bupt_volumetric/clang-runtime/bupt_volumetric_receiver.exe`

---

## 3. 怎么跑 + 怎么看结果

**一键启动（推荐）**——一条命令拉起信令服务器 + 接收端 + 发送端：

```powershell
& "D:\BUPT-XR\bupt_volumetric\scripts\run_loopback.ps1"
# 可加参数：-RunId demo1 -Seed 42 -SignalingPort 39000
# 只启动某个角色：-Role Server / Sender / Receiver
```

发送端可加运行参数：`--frames 60`（帧数）、`--fps 30`（提交帧率）、`--bitrate 8000000`（BWE 启动前的 bootstrap 预算；注意这不是 pin 死 GoogCC 的估计值）。

脚本会：启动信令服务器（隐藏）→ 启动接收端（弹出延迟窗口）→ 启动发送端 → 发送端发完退出后提示你**关闭接收端窗口**结束演示并回收服务器。

**手动启动**（三个终端，默认端口 9090）：

```powershell
$bin = "D:\BUPT-XR\out\bupt_volumetric\clang-runtime"

# 1) 信令服务器
& "$bin\bupt_volumetric_signaling_server.exe" --run-id=run1 --result-dir=out --role=signaling

# 2) 接收端（另一个终端）
& "$bin\bupt_volumetric_receiver.exe" --run-id=run1 --result-dir=out --role=receiver --peer-id=receiver --seed=7

# 3) 发送端（再一个终端）
& "$bin\bupt_volumetric_sender.exe" --run-id=run1 --result-dir=out --role=sender --peer-id=sender --seed=7
```

**发送端**跑完约 1 秒后退出并打印：

```
sender: signaling connected
sender: summary {
  rtt_ms=0                  # 候选对 RTT（localhost 约 0 ms）
  target_bitrate_bps=600000 # GoogCC 目标码率
  outbound_packets=69 outbound_bytes=65216   # 实际发出的 source 包/字节
  fec_packets_sent=8 fec_bytes_sent=8896     # 实际发出的 repair(FEC) 包/字节
  pacer_delay_ms=0          # pacer 排队延迟
  total_packet_send_delay_ms=983             # 发送队列总延迟
}
```

**接收端**会弹出一个 **GDI 窗口（无画面、黑底），持续刷新延迟与指标文本**（关闭窗口即退出）：

```
┌ BUPT-Volumetric Receiver (latency) ──────┐
│ latency p50=319.0 p90=319.0 p99=325.0 ms │   ← 端到端块延迟（捕获→解码）
│ (n=30)                                    │
│ completed_frames=5  completed_blocks=0    │   ← 完成块/帧计数
│ loss=0.00  jitter_ms=0.0                 │   ← 丢包率 / 抖动
└──────────────────────────────────────────┘
```

（同窗口下 stderr 也会打印 `receiver: signaling connected`。）

> **「延迟」是什么**：窗口第一行是 `LatencyRecorder` 算出的**端到端块延迟**（发送端把捕获时间嵌进 access-unit 头，接收端解码时减去它——捕获→解码，localhost 下约 300 ms，主要是 pacer/编码/FEC 排队，不是网络 RTT）。发送端 stderr 另打 `rtt_ms / pacer_delay_ms / total_packet_send_delay_ms` 这几个**传输层**延迟。两者合起来就是完整链路：应用/编码/pacer 排队（端到端） + 网络往返（RTT）。

---

## 4. 当前已验证什么（证据）

| 能力 | 证据 |
|---|---|
| 语义分块 FEC 生成 | `fec_packets_sent > 0`（本 README 上方输出）；P0 128/P1 32/P2 0 目标经诊断确认 repair 数匹配策略 |
| 真实 WebRTC 传输 | `localhost_media_flow_test`（RTP/SRTP/pacer 真实流动）|
| SRTP 加密生效 | `srtp_confidentiality_test`（OCTREE-SIM magic 在明文、绝不在密文）|
| TWCC/RTCP 反馈 | `twcc_feedback_flow_test`（RTCP RR 回传 loss/jitter）|
| 三进程环回 | 上节 smoke 输出（三个独立进程真实 UDP/ICE/DTLS）|
| 信令/控制/预算/组装 | `signaling_*`、`control_channel`、`run_manifest`、`access_unit` 等单测 |

全量测试：运行时预设 **146/146**，纯算法预设 **92/92**。

```powershell
ctest --preset windows-clang-runtime
ctest --preset windows-clang-release
```

---

## 5. 已知限制（诚实声明）

1. **无渲染**：接收端窗口是黑底、只显示指标文本，不生成点云图像。
2. **`completed_blocks=0`**：运行时发送路径当前发的是「整块 access unit（28 字节 envelope）」，不是计划里的「逐符号 48 字节 wire header」，所以 wire-header 级的 `BlockAssembler`/`SemanticPayloadObserverAdapter` 通路在当前格式下不产块（解码器层的 `completed_frames` 才是当前逐块完成信号）。要等编码器切到 `SemanticPacketizer` 源符号后才激活（已记录在 `docs/progress.md`）。
3. **端到端块延迟已接线**：`latency` 模块（`LatencyClock` + `LatencyRecorder`）已完成；发送端把捕获时间（QPC，同机跨进程共享时钟）写进 access-unit 头，接收端解码时算出「捕获→解码」延迟并显示在窗口（localhost 约 300 ms，主要是 pacer/编码/FEC 排队）。更细的「generate→admit→encode→send→receive→block available」分段拆分仍是后续。
4. **无丢包/弱网**：当前是理想 localhost 环回，`loss_fraction=0`。弱网矩阵、FEC 恢复对照实验是后续任务（Task 18）。
5. 信令端口固定 9090、媒体走真实 localhost UDP host candidate，无 STUN/TURN。

---

## 6. 代码脉络

```
include/bupt_volumetric/{model,simulation,transport,protection,receiver,rtc,signaling,metrics,config,latency}
src/                (同上，实现)
src/apps/           三个进程 main + 信令环回装配
extern/webrtc/src   最小补丁（见 webrtc_patches/manifest.json）
test/test_volumetric_*  测试（纯算法 + 运行时）
```

关键补丁（`webrtc_patches/manifest.json`）：per-block FEC override（`rtp_video_sender.cc` 等）、post-FEC source observer（`rtp_video_stream_receiver2.*`）、发送侧 FEC 统计（`rtcstats_objects.*`）、SRTP 发送观察者（`srtp_transport.*`）。

实施计划见 `docs/plans/`，逐任务进度见 `docs/progress.md`。
