# Localhost WebRTC Semantic-FEC Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不绕过 WebRTC 网络栈的前提下，把现有算法骨架扩展为可验证的本机真实环回系统：发送端生成 2,000,000 字节模拟八叉树帧，按 P0/P1/P2 语义等级进行准入和差异化 FEC，经 PeerConnection、ICE、DTLS、SRTP、RTP/RTCP、TWCC/GoogCC 和 WebRTC pacer 发出；接收端完成原生 FEC 恢复、语义块组装和延迟/网络指标输出。

**Architecture:** 采用“应用语义核心 + WebRTC 媒体适配层 + 最小上游补丁”三层结构。应用核心不依赖 WebRTC，负责线格式、语义块、预算、deadline 和接收端可用性；WebRTC 适配层把模拟体积帧作为自定义 `OCTREE-SIM` 视频格式接入编码/解码回调，并消费 GoogCC 的 `SetRates`；WebRTC 原生栈负责 ICE/DTLS/SRTP、TWCC、GoogCC、pacer、RTP/RTCP 和 ULPFEC。上游补丁只补足“每个语义块独立设置 FEC、明确 flush、恢复后逐包观察”三个原生 API 当前缺口，不在应用层重造 UDP、拥塞控制或 FEC 编解码器。

**Tech Stack:** C++20、CMake 3.25+、Ninja、Windows x64 clang-cl、固定提交 `8a3d684776938a4614f892f81859136ff4855ea3` 的原生 libwebrtc、GoogleTest/GoogleMock、PowerShell、JSONL/CSV。

---

## 0. 本计划的性质与硬约束

本文件是实施计划，不是实现。本轮只新增本计划文件，不修改 `.h/.cc/CMakeLists.txt`、WebRTC 源码、配置或脚本。

实施期间必须遵守以下约束：

- `D:\BUPT-XR` 不是 Git 仓库，不能假设根目录存在提交历史。若后续为 `bupt_volumetric` 单独建 Git 仓库，应由用户明确授权；在此之前以测试结果、构建清单和补丁 manifest 作为检查点。
- `extern\webrtc\src` 是故意保持 dirty 的 vendored checkout。禁止 `git reset --hard`、`git checkout --`、`git clean`、批量换行转换、例行 `gclient sync` 或无关的全量重建。
- 每次修改 WebRTC 前重新核对 `extern\webrtc\src\GEMINI.md`，并使用 `git -c core.safecrlf=false diff --ignore-cr-at-eol` 审核真正差异。
- 应用自有代码留在 `bupt_volumetric`；只有无法从公开 API 完成的最小接缝才进入 `extern\webrtc\src`。
- 不从 `bupt_pc` 复制“最小码率等于最大码率”的设置。那会让 GoogCC 失去调节空间。本项目只设安全上限和合理启动值，实时预算来自 WebRTC 的目标码率回调。
- 不把 DataChannel 当媒体通道。DataChannel 只允许承载配置、开始/停止、运行 ID、时钟校准消息和实验控制。
- 不关闭、不旁路 WebRTC pacer；不从应用代码直接 `sendto` 媒体 RTP 包。
- 第一版 OCTREE-SIM 媒体全部关闭 RTX/NACK 重传；P0/P1 依赖 FEC，P2 可丢。原因是过期的点云块没有重传价值，而且 stock RTX 无法天然理解语义块 deadline。P0-only 限时 RTX 仅作为后续对照实验，不进入第一版验收。
- 每项功能都执行“先写失败测试 → 最小实现 → 测试通过 → 重构 → 全量回归”。新发现的缺陷必须先固化为 `test_volumetric_*` 或 WebRTC 上游单测，再修复。

## 1. 已敲定的唯一技术路线

### 1.1 进程与链路

最终本机环回仍使用三个独立进程，而不是把所有对象塞在一个测试进程里：

```text
bupt_volumetric_sender.exe
  模拟帧 -> 语义分块 -> 准入/丢弃 -> OCTREE-SIM encoder
  -> RTP packetizer -> ULPFEC -> WebRTC pacer -> SRTP/UDP
                   | SDP/ICE/control
                   v
bupt_volumetric_signaling_server.exe
                   ^
                   | SDP/ICE/control
bupt_volumetric_receiver.exe
  UDP/SRTP -> RED/ULPFEC recovery -> OCTREE-SIM depacketizer
  -> 语义块组装 -> 区域/帧可用性 -> metrics
```

本机默认只产生 host ICE candidates，不配置 STUN/TURN；ICE 仍必须真实执行。DTLS/SRTP 即便在 localhost 也不得关闭。最终跨主机时只替换信令地址和 ICE server 配置，不改媒体架构。

### 1.2 媒体与 FEC

- SDP 媒体格式名固定为 `OCTREE-SIM/90000`，RTP 时钟固定 90 kHz。
- 应用 payload 内有版本化体积视频线头；RTP 头仍由 WebRTC 生成。
- 每个语义块最多 `K=32` 个 source symbols，每个目标 payload 1,150 B；实际最大 payload 必须再扣除自定义线头、RTP 扩展、SRTP 和 RED/FEC 所需空间，由 packetizer 依据 WebRTC 给出的 `PayloadSizeLimits` 决定，不能硬写 MTU。
- P0：目标 repair 比例 50%，标准满块 `32 source + 16 repair`。
- P1：目标 repair 比例 12.5%，标准满块 `32 source + 4 repair`。
- P2：source only，不生成 FEC，不允许 RTX。
- FEC 采用 WebRTC 现有 ULPFEC/RED 编解码器。应用只提供语义块边界与目标 repair 数量；repair 包生成、pacing、SRTP 和接收恢复由 WebRTC 完成。
- 第一版每个 FEC block 独立生成与 flush，绝不让 P0/P1/P2 混入同一个 FEC protection set。

### 1.3 拥塞控制与准入

WebRTC 的 TWCC/GoogCC 是唯一网络容量控制器。应用层不是第二套拥塞控制器，而是其“内容适配执行器”：

1. WebRTC 通过 encoder `SetRates(RateControlParameters)` 给出目标媒体码率；
2. 适配器把目标码率扣除预计 RTP/SRTP/FEC/RTCP 开销，形成当前调度窗口的 payload budget；
3. admission controller 以完整语义块为单位准入，顺序固定为 P0 → P1 → P2；
4. P0/P1 的预算必须包含 repair；不能先把 source 塞满再额外超发 FEC；
5. 已过 deadline 的未发送块直接淘汰；不得让旧帧在 pacer 前无限排队；
6. pacer 仍决定最终发送时序，应用只控制什么内容有资格进入它。

第一版不靠轮询 RTCStats 做高速闭环：`SetRates` 是实时控制入口，RTCStats 只用于低频观测、交叉校验和报表。

## 2. 当前基线与必须纠正的预算数字

当前已实现并有 12 个算法测试覆盖：

- 每帧 2,000,000 B；
- P0 250,000 B，P1 750,000 B，P2 1,000,000 B；
- 1,150 B source symbol，最多 32 source symbols/block；
- P0 50%、P1 12.5%、P2 0%，尾块向上取整；
- 预计 P0 7 块、P1 21 块、P2 28 块，共 56 块。

必须在实现前修正文档和测试中的一个易混淆数字：

- 按连续字节比例计算的“理论 repair”是 218,750 B；
- 但 repair 以完整 1,150 B symbol 生成，当前尾块向上取整得到 P0 109 个、P1 82 个，共 191 个 repair symbols；
- 因此实际 packetized repair payload 是 `191 * 1,150 = 219,650 B`；
- source + repair payload 是 2,219,650 B；
- 这仍未包含自定义线头、RTP header/extensions、RED/ULPFEC、SRTP、UDP/IP 和 RTCP。

测试与报表必须同时展示“理论比例预算”和“实际整包预算”，禁止混用二者。

## 3. 完成定义：不是“能启动”，而是形成证据链

以下条件全部满足，才能宣称 pipeline 打通：

| 能力 | 必须提供的证据 | 不足以作为证据的东西 |
|---|---|---|
| FEC repair 真生成 | sender 统计 source/repair 包数与字节；确定性丢包下 receiver recovered 包数大于 0，数据校验正确 | 仅配置 FEC 比例或 SDP 出现 `ulpfec` |
| 线格式/RTP packetizer | golden vectors、乱序/截断/未知版本测试；接收逐块还原 byte-exact | 只测内存结构大小 |
| 带宽准入/丢弃 | 容量阶跃时 P2 先降、P0 最后降；队列有界；过期块不发送 | 只打印目标码率 |
| PeerConnection | offer/answer、ICE gathering、candidate-pair succeeded/selected、connection state connected | 只创建 factory |
| DTLS/SRTP | DTLS connected、加密套件/证书信息可观测，抓包看不到 OCTREE-SIM 明文 payload | localhost UDP 有包 |
| TWCC/GoogCC | SDP transport-cc 扩展、RTCP transport feedback 计数增长、`SetRates` 随容量变化 | 手工修改应用码率 |
| WebRTC pacer | RTP egress/pacer queue delay 与 first/last packet send time 可观测，媒体没有应用层直发 | 包到达间隔看起来均匀 |
| RTP/RTCP | RTP sequence/timestamp 连续性、RTCP SR/RR/TWCC 统计增长 | 信令消息传输成功 |
| 接收恢复和组装 | source 丢失后由 FEC 恢复；P2 缺失不阻塞 P0/P1 可用 | 完整帧无丢包时能拼接 |
| 真实 localhost | 三个进程、真实 UDP socket/ICE candidate pair、非 fake transport 完成 60 秒运行 | 单进程函数调用或模拟网络测试 |

## 4. 分层与依赖规则

### 4.1 应用核心层：不得 include WebRTC

负责：

- `wire_header` 序列化/反序列化；
- source symbol 与 block 规划；
- 语义优先级和 deadline；
- transport budget 与 admission；
- receiver block assembly 与 frame availability；
- 与具体传输无关的指标事件。

### 4.2 WebRTC 适配层：实现边界接口

负责：

- OCTREE-SIM encoder/decoder factory；
- 将 `SetRates` 转换为应用预算；
- 将语义块元数据传给 packetizer/FEC seam；
- 将恢复后的 RTP payload 交给应用 assembler；
- 将 PeerConnection/RTCStats/RTCP/pacer 观测转换成统一指标。

### 4.3 框架与进程层

负责：

- PeerConnection factory、线程和生命周期；
- SDP/ICE 信令；
- localhost 配置；
- sender/receiver/signaling 三个进程；
- 运行 manifest、日志、退出码和结果目录。

依赖只能由外向内。`transport_budget`、`semantic_admission_controller` 和 `block_assembler` 不得引用 `webrtc::*` 类型。

## 5. 逐任务实施计划

### Task 0：冻结基线、建立可重复的 provenance 记录

**Files:**

- Modify: `bupt_volumetric/docs/progress.md`
- Modify: `bupt_volumetric/webrtc_patches/manifest.json`
- Test: `test/test_volumetric_build_contract/`

- [ ] 记录 WebRTC commit、`out/Default/args.gn` 摘要、`webrtc.lib` 时间/大小、应用编译器和 CMake preset。
- [ ] 记录实施前有意义的 upstream dirty diff，使用 `--ignore-cr-at-eol`，不把换行噪声写入 manifest。
- [ ] 运行当前基线：

  ```powershell
  cmake --preset windows-clang-release -S D:\BUPT-XR\bupt_volumetric
  cmake --build --preset windows-clang-release
  ctest --preset windows-clang-release
  ```

- [ ] 验收：12 个现有算法测试全部通过；manifest 能区分“原有用户改动”和“本项目新增 patch”。
- [ ] 若基线不通过，先把失败记入 progress，停止网络集成，不以修改测试绕过失败。

### Task 1：定义稳定、可演进的 wire format

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/transport/wire_header.h`
- Modify: `bupt_volumetric/src/transport/wire_header.cc`
- Modify: `bupt_volumetric/docs/architecture/wire-format.md`
- Test: `test/test_volumetric_packetization/wire_header_test.cc`
- Test: `test/test_volumetric_packetization/wire_header_fuzz_cases_test.cc`

- [ ] 先写失败测试，覆盖 big-endian/network byte order、round-trip、golden bytes、截断、超长、未知版本、非法 class、非法 symbol index、repair/source 标志冲突和整数溢出。
- [ ] V1 头至少包含：magic、version、flags、frame_id、90 kHz timestamp、region_id、semantic_class、block_id、source_symbol_count、repair_symbol_count、symbol_index、payload_bytes、capture/generation tick 或 latency token、block deadline。
- [ ] header 使用显式字段写入，不允许直接 `memcpy(struct)`，避免 padding 和端序问题。
- [ ] 明确 `symbol_index < K` 为 source；repair 包的索引/身份以 WebRTC FEC 结果为准，不伪造为源 symbol。
- [ ] 验收：golden vector 在 Debug/Release、重复运行中完全一致；解析器对所有不合法输入 fail closed。

### Task 2：实现 source packetizer 与 payload-size 契约

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/transport/semantic_packetizer.h`
- Modify: `bupt_volumetric/src/transport/semantic_packetizer.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/transport/block_planner.h`
- Modify: `bupt_volumetric/src/transport/block_planner.cc`
- Test: `test/test_volumetric_packetization/semantic_packetizer_test.cc`
- Test: `test/test_volumetric_packetization/payload_limit_test.cc`

- [ ] 先写失败测试，覆盖单包、满 32-symbol 块、尾块、payload limit 缩小、header 放不下、frame_id wrap、精确 2,000,000 B 覆盖和 byte-exact reassembly。
- [ ] packetizer 接收“WebRTC 当前允许的最大 payload”而不是假设 MTU；1,150 B 是目标上限，不是无条件结果。
- [ ] 每个 source packet 明确携带 `block_begin/block_end`；RTP marker 只表示一个 WebRTC access unit 结束，不能再隐式兼任所有语义块边界。
- [ ] 验收：源数据每个字节恰好出现一次，不重不漏；任何包都不超过传入的 payload limit。

### Task 3：实现精确开销模型与 transport budget

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/transport/transport_budget.h`
- Modify: `bupt_volumetric/src/transport/transport_budget.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/protection/protection_budget.h`
- Modify: `bupt_volumetric/src/protection/protection_budget.cc`
- Test: `test/test_volumetric_protection/protection_budget_test.cc`
- Test: `test/test_volumetric_congestion/transport_budget_test.cc`

- [ ] 新增测试目录 `test/test_volumetric_congestion` 并接入 CTest。
- [ ] 测试同时断言理论 repair 218,750 B 和实际整 symbol repair 219,650 B，不允许二者互换。
- [ ] 分开统计 source payload、application header、RTP header/extensions、RED/ULPFEC、SRTP、UDP/IP、RTCP 和 padding；第一版无法精确预知的项用保守 estimator，并在运行时用真实 stats 校准。
- [ ] budget 的输入是 `target_rate × scheduling_window`；输出是允许进入 encoder/packetizer 的完整 block 集合和预计 wire bytes。
- [ ] 验收：不会把 FEC 当免费字节；不会在 source 已吃满目标码率后继续超发 repair。

### Task 4：实现语义准入、过期淘汰和有界队列

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/transport/semantic_admission_controller.h`
- Modify: `bupt_volumetric/src/transport/semantic_admission_controller.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/transport/transport_feedback.h`
- Modify: `bupt_volumetric/src/transport/transport_feedback.cc`
- Test: `test/test_volumetric_congestion/semantic_admission_controller_test.cc`
- Test: `test/test_volumetric_congestion/deadline_eviction_test.cc`
- Test: `test/test_volumetric_congestion/rate_step_test.cc`

- [ ] 先用人工注入的目标码率测试，不依赖 PeerConnection。
- [ ] 准入粒度固定为完整语义 block；不允许发送半个 block 后因预算耗尽留下不可恢复垃圾。
- [ ] 同一帧顺序为 P0 → P1 → P2；跨帧优先新鲜 P0，旧 P1/P2 到 deadline 立即淘汰。
- [ ] 加入 hysteresis 和最小保持周期，防止目标码率小幅波动导致 P1/P2 频繁开关。
- [ ] 队列上限同时按 byte、block、frame age 三个维度限制；达到上限优先丢旧 P2，再旧 P1，P0 最后。
- [ ] 验收：容量从高到低再恢复的确定性测试中，P2 最先下降、P0 最稳定；队列永不无限增长；恢复后不补发过期块。

### Task 5：实现接收端独立 block assembler

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/receiver/block_assembler.h`
- Modify: `bupt_volumetric/src/receiver/block_assembler.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/receiver/frame_availability_tracker.h`
- Modify: `bupt_volumetric/src/receiver/frame_availability_tracker.cc`
- Test: `test/test_volumetric_receiver/block_assembler_test.cc`
- Test: `test/test_volumetric_receiver/frame_availability_tracker_test.cc`

- [ ] 新增 `test/test_volumetric_receiver` 并接入 CTest。
- [ ] 覆盖乱序、重复、缺包、恢复包、跨帧错配、非法 block、deadline、尾 symbol 实际长度和内存上限。
- [ ] assembler 只接受通过 wire parser 校验的 source payload；FEC repair 不直接进入业务 assembler。
- [ ] P0/P1 block 完成后立即发布可用事件，不等待 P2，也不等待“整帧 marker”。
- [ ] 验收：丢失任意 P2 不阻塞 P0/P1 可用；重复包不重复计数；过期 frame 能释放全部内存。

### Task 6：建立不依赖 WebRTC 的确定性端到端模型测试

**Files:**

- Test: `test/test_volumetric_loopback_e2e/semantic_pipeline_model_test.cc`
- Test: `test/test_volumetric_loopback_e2e/packet_loss_model_test.cc`
- Test: `test/test_volumetric_loopback_e2e/queue_bound_model_test.cc`

- [ ] 把 frame generator → partitioner → block planner → packetizer → loss/reorder injector → assembler 串起来。
- [ ] 此阶段的 loss injector 只验证应用层语义行为，不冒充真实 FEC 或真实网络。
- [ ] 对固定 seed 保存 golden summary：发送/准入/丢弃/完成/过期 blocks、payload bytes 和 reason code。
- [ ] 验收：测试名称和日志明确带 `model`，不得把结果写成 WebRTC loopback 性能。

### Task 7：WebRTC 接缝可行性闸门（先证明，再扩展）

**Files:**

- Create: `bupt_volumetric/docs/architecture/webrtc-seam-spike-results.md`
- Modify: `bupt_volumetric/docs/architecture/webrtc-patch-boundary.md`
- Modify: `bupt_volumetric/webrtc_patches/manifest.json`
- Test candidate: `extern/webrtc/src/modules/rtp_rtcp/source/ulpfec_generator_unittest.cc`
- Test candidate: `extern/webrtc/src/video/rtp_video_stream_receiver2_unittest.cc`

- [ ] 在动上游前验证并记录以下事实：`kVideoCodecGeneric` 能承载自定义 factory 返回的 `OCTREE-SIM` SDP 名；generic packetizer 是否会改写 payload；恢复包何时设置 `packet.recovered()`；pacer 前后可观测点；ULPFEC protection set 的最大 source 包数。
- [ ] 写最小失败测试证明当前 `UlpfecGenerator` 只能靠 RTP marker flush，且 `kNumImportantPackets=0`、`kUseUnequalProtection=false`，无法按语义 block 直接控制。
- [ ] 写最小失败测试证明接收端需要一个“ULPFEC 恢复后、整视频帧组装前”的逐包观察点，否则 P2 缺失会阻塞 P0/P1 独立可用。
- [ ] 形成 go/no-go 决策：
  - Go：可用不超过三个窄接口解决，进入 Task 8；
  - No-go：停止修改 WebRTC，记录阻点，重新评估独立 RTP data API 或应用层 FEC；未经用户确认不自动换路线。
- [ ] 验收：spike 文档列出准确类、方法、线程归属、生命周期和测试目标，不只写概念图。

### Task 8：最小 WebRTC upstream patch 与上游单测

**候选文件（Task 7 后冻结准确集合）：**

- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/video_fec_generator.h`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/ulpfec_generator.h`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/ulpfec_generator.cc`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/ulpfec_generator_unittest.cc`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/rtp_sender_egress.h`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/rtp_sender_egress.cc`
- Modify: `extern/webrtc/src/video/rtp_video_stream_receiver2.h`
- Modify: `extern/webrtc/src/video/rtp_video_stream_receiver2.cc`
- Modify: `extern/webrtc/src/video/rtp_video_stream_receiver2_unittest.cc`
- Modify: only the directly affected `BUILD.gn` files

- [ ] 先加上游失败测试，再添加三个最小能力：显式 FEC block begin/end/flush；每个 block 的 protection params；恢复后 payload observer（含 recovered 标志、sequence、timestamp、receive time）。
- [ ] 不复制 FEC 算法，不引入 P0/P1 业务类型到 WebRTC；上游只接收通用的 block metadata/importance count/protection params。
- [ ] 保留 stock 行为为默认值，未启用语义扩展时所有现有视频路径行为不变。
- [ ] 优先复用 `ForwardErrorCorrection::EncodeFec` 已存在的 `num_important_packets/use_unequal_protection` 能力；若 V1 每块已经独立，则 UEP 首版可保持关闭，以“分块 + 不同比例”实现语义保护，避免两套 UEP 叠加。
- [ ] 使用 bundled Ninja 构建最窄目标，目标名在 Task 7 通过 `gn desc` 确认后写入 progress，例如：

  ```powershell
  cd D:\BUPT-XR\extern\webrtc\src
  .\third_party\ninja\ninja.exe -C out\Default modules_rtp_rtcp_unittests video_unittests
  ```

- [ ] 只运行相关 gtest filter，然后运行受影响目标的完整单测。
- [ ] 验收：32 source 的 P0 block 精确生成 16 repair、P1 精确生成 4 repair、P2 生成 0；相邻 block 不交叉保护；flush 后内部状态归零；recovered observer 只回调恢复后的 source payload，不把 repair 当业务包。

### Task 9：接入 WebRTC 构建依赖与 OCTREE-SIM factory

**Files:**

- Modify: `bupt_volumetric/cmake/WebRtcDependency.cmake`
- Modify: `bupt_volumetric/CMakeLists.txt`
- Modify: `bupt_volumetric/CMakePresets.json`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder_factory.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_encoder_factory.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_encoder.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_decoder.h`
- Modify: `bupt_volumetric/src/rtc/octree_sim_decoder.cc`
- Test: `test/test_volumetric_rtc/octree_sim_codec_factory_test.cc`
- Test: `test/test_volumetric_rtc/octree_sim_encoder_rate_test.cc`

- [ ] 新增 runtime preset，保留当前 runtime OFF 的纯算法 preset，避免每次单测都强制链接整个 WebRTC。
- [ ] 导入现有 `out/Default/obj/webrtc.lib` 与必需系统库，并加 provenance check，禁止默默链接旧的未含补丁产物。
- [ ] factory SDP name 固定 `OCTREE-SIM`，内部 codec type 使用经 Task 7 证明可行的 `kVideoCodecGeneric` 路径，不为一个研究原型扩展全局 codec enum，除非测试证明不可行。
- [ ] encoder 不压缩数据；它从线程安全 frame source 取得已经 packetizable 的 access unit。输入 VideoFrame 只作为 WebRTC 调度触发，不把 2 MB 数据塞进像素平面。
- [ ] `SetRates` 必须保存 target bitrate/framerate 并推送给 admission controller；单测覆盖降码率、升码率、零码率暂停和线程安全。
- [ ] 验收：factory 能被 PeerConnection factory 查询并创建；不得固定 min=max bitrate；编码输出大小受准入预算约束。

### Task 10：PeerConnection、localhost ICE、DTLS/SRTP 生命周期

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/rtc_configuration.h`
- Modify: `bupt_volumetric/src/rtc/rtc_configuration.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h`
- Modify: `bupt_volumetric/src/rtc/peer_connection_session.cc`
- Test: `test/test_volumetric_rtc/rtc_configuration_test.cc`
- Test: `test/test_volumetric_rtc/peer_connection_lifecycle_test.cc`
- Test: `test/test_volumetric_rtc/localhost_ice_dtls_test.cc`

- [ ] 参考 `bupt_pc` 的线程/factory/offer-answer/ICE 生命周期，但重新实现适合本项目的窄接口。
- [ ] 默认 bundle policy、RTCP mux 和 unified-plan；localhost 默认空 ICE server 列表，只用 host candidates。
- [ ] 测试创建两个真实 PeerConnection，经内存信令交换 SDP/candidates，但媒体走真实 localhost network stack。
- [ ] 断言 signaling、ICE gathering、ICE connection、PeerConnection、DTLS 状态按预期迁移，并测试失败/超时/重复 candidate/有序关闭。
- [ ] 验收：selected candidate pair 是 localhost/host UDP；DTLS connected；发送端关闭后所有 WebRTC 线程可在超时内 join，无泄漏/死锁。

### Task 11：TWCC、GoogCC 和 pacer 的控制与观测接线

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h`
- Modify: `bupt_volumetric/src/rtc/peer_connection_session.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/transport/transport_feedback.h`
- Modify: `bupt_volumetric/src/transport/transport_feedback.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/metrics/metric_event.h`
- Test: `test/test_volumetric_congestion/webrtc_rate_callback_test.cc`
- Test: `test/test_volumetric_congestion/twcc_negotiation_test.cc`
- Test: `test/test_volumetric_metrics/rtc_stats_mapping_test.cc`

- [ ] SDP 测试断言 transport-wide congestion control RTP extension 和相应 RTCP feedback 真正协商成功。
- [ ] 高频控制路径为 WebRTC → encoder `SetRates` → transport feedback adapter → admission controller。
- [ ] 低频 stats 路径采集 candidate-pair RTT/availableOutgoingBitrate、outbound bytes/packets、retransmitted bytes、totalPacketSendDelay、remote-inbound loss/jitter/RTT，以及能取得的 pacer/queue 指标。
- [ ] 若公开 RTCStats 不包含所需 assembly/pacer queue 深度，在 Task 8 的最小补丁里增加只读 observer，不通过猜测派生为“实测值”。
- [ ] 在 WebRTC fake network/call test 中做容量阶跃，断言 `SetRates` 有方向正确的变化，并最终驱动 P2/P1 准入变化。
- [ ] 验收：不是应用定时器手工改码率；日志能同时看到 BWE target、admitted payload、estimated overhead、pacer delay 和 actual sent bitrate。

### Task 12：原生 FEC 发送路径完整接入

**Files:**

- Modify: `bupt_volumetric/src/rtc/octree_sim_encoder.cc`
- Modify: `bupt_volumetric/src/transport/semantic_packetizer.cc`
- Modify: WebRTC seam files frozen in Task 8
- Test: `test/test_volumetric_rtc/native_fec_sender_test.cc`
- Test: `test/test_volumetric_protection/fec_wire_accounting_test.cc`

- [ ] encoder/packetizer 为每个 admitted block 发出显式 begin/add/end，P0/P1 设置各自 repair target，P2 显式 0。
- [ ] repair 与 source 统一进入 WebRTC egress/pacer/SRTP，不允许 repair 从旁路 socket 发送。
- [ ] 统计区分 `source_packets/source_bytes`、`repair_packets/repair_bytes`、header/RED overhead 和 padding。
- [ ] 测试满块与尾块；不能只测试 32+16 和 32+4，还要验证最后不足 32 source 时的取整规则。
- [ ] 验收：实际生成数量与 protection policy 一致；账本与 WebRTC egress 实际 packet counters 对齐，允许的差异只能来自已记录的 padding/RTCP。

### Task 13：原生 FEC 恢复到语义块组装

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/semantic_payload_observer_adapter.h`
- Modify: `bupt_volumetric/src/rtc/semantic_payload_observer_adapter.cc`
- Modify: `bupt_volumetric/src/receiver/block_assembler.cc`
- Test: `test/test_volumetric_receiver/native_fec_recovery_test.cc`
- Test: `test/test_volumetric_receiver/post_fec_observer_test.cc`

- [ ] observer 收到原始或恢复后的 source RTP payload 后统一解析 wire header，并将 `recovered`、sequence number、receive time 保留到指标事件。
- [ ] 确定性删除 P0/P1 block 内不超过 repair 能力的 source 包，断言恢复数据 byte-exact、block 完成且 recovered counter 正确。
- [ ] 删除超过能力的 source 包，断言 block 不被伪报完成；其他 block 仍可独立发布。
- [ ] 删除任意 P2 包，断言 P0/P1 的发布不等待 P2。
- [ ] 验收：恢复发生在 WebRTC ULPFEC receiver，不是在应用测试里补回字节；日志能追溯 lost sequence → repair received → source recovered → block available。

### Task 14：真实 RTP/RTCP、SRTP 与反馈证据测试

**Files:**

- Test: `test/test_volumetric_rtc/rtp_rtcp_transport_test.cc`
- Test: `test/test_volumetric_rtc/srtp_confidentiality_test.cc`
- Test: `test/test_volumetric_rtc/twcc_feedback_flow_test.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/metrics/run_manifest.h`
- Modify: `bupt_volumetric/src/metrics/run_manifest.cc`

- [ ] 断言 RTP sequence、90 kHz timestamp、SSRC、payload type 和 marker/block semantics。
- [ ] 断言 RTCP SR/RR 与 transport feedback counters 随时间增长。
- [ ] 测试 harness 在加密前持有已知明文 token，并在 loopback UDP 抓取层确认线上的 SRTP payload 不出现该 token；不记录密钥，不把抓包永久保存为默认产物。
- [ ] 静态/构建测试扫描应用媒体路径，禁止 Winsock `sendto/WSASendTo` 绕过 PeerConnection；信令 server 可使用自己的 socket，但必须与媒体目标分离。
- [ ] 验收：可以用 stats + 测试 observer 证明 RTP/RTCP 真实流动、SRTP 生效、TWCC feedback 返回。

### Task 15：信令、控制 DataChannel 与三个运行进程

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/signaling/protocol.h`
- Modify: `bupt_volumetric/src/signaling/protocol.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/signaling/signaling_server.h`
- Modify: `bupt_volumetric/src/signaling/signaling_server.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/signaling/signaling_client.h`
- Modify: `bupt_volumetric/src/signaling/signaling_client.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/rtc/control_channel.h`
- Modify: `bupt_volumetric/src/rtc/control_channel.cc`
- Modify: `bupt_volumetric/src/apps/signaling_server_main.cc`
- Modify: `bupt_volumetric/src/apps/sender_main.cc`
- Modify: `bupt_volumetric/src/apps/receiver_main.cc`
- Test: `test/test_volumetric_signaling/protocol_test.cc`
- Test: `test/test_volumetric_signaling/server_integration_test.cc`
- Test: `test/test_volumetric_rtc/control_channel_test.cc`

- [ ] 信令协议仅承载 session registration、offer、answer、ICE candidate 和错误；设置最大消息尺寸和严格 JSON/schema 校验。
- [ ] control DataChannel 为 reliable + ordered，但只发 run config、start/stop、clock ping/pong 和完成摘要；测试拒绝超过控制消息上限的媒体 payload。
- [ ] 所有进程支持 `--config`、`--run-id`、`--result-dir`、明确退出码和 Ctrl+C 有序退出。
- [ ] 验收：进程启动顺序可变；接收端/发送端短暂重连不会让 signaling server 崩溃；错误配置在发送任何媒体前失败。

### Task 16：延迟与指标体系

**Files:**

- Modify: `bupt_volumetric/include/bupt_volumetric/latency/latency_clock.h`
- Modify: `bupt_volumetric/src/latency/latency_clock.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/latency/latency_recorder.h`
- Modify: `bupt_volumetric/src/latency/latency_recorder.cc`
- Modify: `bupt_volumetric/include/bupt_volumetric/latency/clock_sync_estimator.h`
- Modify: `bupt_volumetric/src/latency/clock_sync_estimator.cc`
- Modify: `bupt_volumetric/src/metrics/metrics_sink.cc`
- Test: `test/test_volumetric_latency/latency_recorder_test.cc`
- Test: `test/test_volumetric_latency/clock_sync_estimator_test.cc`
- Test: `test/test_volumetric_metrics/metrics_sink_test.cc`

- [ ] localhost 同机 E2E 使用同一单调时钟域，不需要 NTP/墙钟；记录 generate、admit、encode callback、first/last RTP enqueue、first/last RTP send、first/last receive、FEC recovered、block available。
- [ ] 跨机能力预留 ping/pong offset+uncertainty estimator；只有不确定度低于阈值才报告校准后单向延迟，否则只报 RTT 和本机分段延迟。
- [ ] 所有指标输出 run manifest、原始 JSONL 和汇总 CSV；至少给 count、min、p50、p90、p95、p99、max，不只给平均值。
- [ ] 区分：应用等待、admission drop、encoder、packetization、pacer queue、network、FEC wait、assembly；无法直接测量的项明确标记 estimated。
- [ ] 验收：空样本、乱序事件、重复事件、跨 frame_id、时钟回退、文件写失败均有测试和明确错误。

### Task 17：三个进程的真实 localhost E2E

**Files:**

- Modify: `bupt_volumetric/scripts/run_loopback.ps1`
- Modify: `bupt_volumetric/docs/operations/run-loopback.md`
- Test: `test/test_volumetric_loopback_e2e/three_process_loopback_test.cc` 或等价 PowerShell 驱动并由 CTest 调用
- Test: `test/test_volumetric_loopback_e2e/result_contract_test.cc`

- [ ] 脚本使用独立临时结果目录和动态空闲端口，启动 signaling → receiver → sender，设置总超时，并保证失败时回收子进程。
- [ ] Case A：单帧 2 MB，验证 byte accounting、P0/P1/P2 block summary。
- [ ] Case B：1 fps × 30 帧，验证持续 RTP/RTCP、TWCC 和无资源泄漏。
- [ ] Case C：5 fps × 60 秒，验证 queue bounded、deadline drop 和指标完整性；若机器容量不足，测试应按预定义门槛失败，而不是自动降低断言。
- [ ] result-contract 测试读取三个进程输出，交叉核对 run-id、frame counts、source/repair packets、recovered blocks、selected candidate pair 和结束原因。
- [ ] 验收：测试必须验证真实 UDP/ICE candidate pair，不能因同进程 shortcut 或 fake network 而通过。

### Task 18：弱网与拥塞回归矩阵

**Files:**

- Modify: `bupt_volumetric/config/scenarios.example.json`
- Modify: `bupt_volumetric/scripts/run_network_matrix.ps1`
- Modify: `bupt_volumetric/docs/operations/run-network-matrix.md`
- Test: `test/test_volumetric_network_resilience/deterministic_loss_test.cc`
- Test: `test/test_volumetric_network_resilience/burst_loss_test.cc`
- Test: `test/test_volumetric_network_resilience/capacity_step_test.cc`
- Test: `test/test_volumetric_network_resilience/reorder_jitter_test.cc`

- [ ] 正确性回归优先使用 WebRTC 的可控 fake network/transport test harness，以固定 seed 精确复现；三进程 Windows 网络整形作为系统级补充，不替代确定性单测。
- [ ] 最小矩阵：

  | 场景 | 容量 | RTT | 丢包 | 抖动/重排 | 主要断言 |
  |---|---:|---:|---:|---:|---|
  | clean | 足够 | 0–2 ms | 0% | 0 | 全部层级可用 |
  | mild | 略低 | 20 ms | 1% random | 2 ms | P2 先降，P0/P1 稳定 |
  | burst | 足够 | 40 ms | burst 4/8/16 packets | 5 ms | 能力内恢复、能力外显式不完整 |
  | constrained | 分级阶跃 | 60 ms | 2% | 10 ms | GoogCC 下调、队列有界 |
  | recovery | 低→高 | 40 ms | 1% | 5 ms | hysteresis 后恢复 P1/P2，无振荡 |

- [ ] 每个场景断言 P0/P1/P2 source、repair、drop、recover、deadline miss，以及 BWE/pacer/queue 曲线方向。
- [ ] 验收：固定 seed 结果可复现；任何生产缺陷都先加入该矩阵或更低层测试再修复。

### Task 19：长稳、关闭路径、打包与文档收口

**Files:**

- Test: `test/test_volumetric_soak/loopback_soak_test.ps1`
- Modify: `bupt_volumetric/packaging/runtime_manifest.json`
- Modify: `bupt_volumetric/README.md`
- Modify: `bupt_volumetric/docs/progress.md`
- Modify: `bupt_volumetric/webrtc_patches/README.md`
- Modify: `bupt_volumetric/webrtc_patches/manifest.json`

- [ ] 先做 10 分钟 CI/开发机 soak，再做手工 2 小时 soak；检查内存、handle、thread、queue、文件增长和 frame age。
- [ ] 测试 receiver 先退、sender 先退、signaling 重启、网络中断恢复和连续运行两次。
- [ ] runtime manifest 列出可执行文件、必须 DLL/数据文件、WebRTC commit、patch checksum、build args 和配置 schema version。
- [ ] 文档明确第一版能力与限制：模拟点云、localhost、无渲染、无真实 G-PCC、无公网性能承诺。
- [ ] 最终验证运行：应用全量 CTest、受影响 WebRTC 单测、三进程 loopback、弱网矩阵 clean/mild/burst、短 soak。
- [ ] 验收：所有测试日志与产物落在唯一 run-id 目录；不存在依赖旧二进制“碰巧成功”的情况。

## 6. 计划中的测试目录结构

最终应至少形成以下测试套件；已有目录继续扩充，不平行制造第二套框架：

```text
D:\BUPT-XR\test\
  test_volumetric_build_contract\
  test_volumetric_model\
  test_volumetric_packetization\
  test_volumetric_protection\
  test_volumetric_congestion\
  test_volumetric_receiver\
  test_volumetric_rtc\
  test_volumetric_signaling\
  test_volumetric_latency\
  test_volumetric_metrics\
  test_volumetric_loopback_e2e\
  test_volumetric_network_resilience\
  test_volumetric_soak\
  test_volumetric_support\
```

测试金字塔的目标不是追求覆盖率数字，而是让失败尽量在最低成本层暴露：

- 纯单元测试：wire、budget、admission、assembler、clock、metrics；
- 组件测试：packetizer/FEC seam、encoder rate callback、PeerConnection lifecycle；
- 集成测试：两个真实 PeerConnection、fake network 弱网；
- E2E：三进程 localhost；
- 系统/长稳：网络矩阵和 soak。

默认测试不得依赖互联网、STUN/TURN 公网服务、管理员权限或固定端口。

## 7. 第一版配置约定

建议固定以下默认值，避免实现阶段反复摇摆：

| 项目 | V1 默认 |
|---|---|
| Frame bytes | 2,000,000 |
| P0/P1/P2 | 1/8、3/8、4/8 |
| Source symbol target | 1,150 B，但服从 WebRTC payload limit |
| Max source symbols/block | 32 |
| P0 repair | ceil(K × 0.50) |
| P1 repair | ceil(K × 0.125) |
| P2 repair | 0 |
| `max_fec_frames` | 1；实际按显式 block flush |
| RTP clock | 90,000 Hz |
| RTX/NACK | OCTREE-SIM 全部关闭 |
| TWCC/GoogCC | 开启且为唯一容量估计器 |
| Pacer | 开启，不 bypass |
| localhost ICE | host UDP candidates，默认无 STUN/TURN |
| DTLS/SRTP | 必须开启 |
| Control channel | reliable + ordered，仅控制消息 |
| Metrics cadence | 控制回调实时；RTCStats 1 Hz 起步 |
| In-flight frames | 默认 1，后续由测试决定是否放宽 |

deadline 不在计划里拍脑袋写死为 10 ms。第一轮实现先将其配置化，并用实际 2 MB 帧率、BWE/pacer 结果确定。所有性能门槛必须在功能正确且指标可信后设定。

## 8. 关键风险与止损条件

### 风险 A：自定义 codec 名只能创建 factory，但 WebRTC 内部仍把它当 generic 整帧处理

止损：Task 7 必须验证 packetizer 与 receive buffer 行为。若 P2 缺失必然阻塞恢复后 observer，只有在窄 observer 接缝可行时继续。

### 风险 B：ULPFEC 依赖 RTP marker flush，语义块无法独立保护

止损：不通过伪造“每个块都是视频帧”污染 timing/GoogCC 语义。加入显式 block flush 的通用 seam，并用现有行为默认关闭保证兼容。

### 风险 C：2 MB/帧在目标帧率下远超当前链路容量

止损：这是 admission/LOD 研究对象，不通过钉死 min=max 码率硬塞。P0/P1/P2 的发送比例必须跟随 GoogCC budget；报告 admitted quality，而不是假装完整帧都发送。

### 风险 D：FEC 数量与“比例”不完全相等

止损：所有决策和报表以整数 source/repair symbols 为主；比例只是派生指标。尾块向上取整的额外 900 B 必须被预算计入。

### 风险 E：WebRTC patch 面不断扩大

止损：若 Task 8 超过“FEC block lifecycle、per-block params、post-FEC observer”三类能力，暂停并重新评审架构，不把业务策略塞入上游。

### 风险 F：localhost 结果被误解为跨网段能力

止损：所有结果 manifest 标明 topology=`localhost-host-candidate`；公网/NAT/TURN、Wi-Fi、跨网段单独建后续 milestone，不复用 localhost 性能结论。

## 9. 阶段检查点

### Checkpoint A：纯应用层闭环

完成 Task 0–6。此时可以声称 wire、budget、admission、assembler 的模型行为正确；不能声称 WebRTC/FEC/network 已打通。

### Checkpoint B：WebRTC 原生接缝成立

完成 Task 7–8。此时可以声称 patched WebRTC 在单元级能按 block 生成/恢复 FEC；不能声称 PeerConnection E2E 已完成。

### Checkpoint C：真实双 PeerConnection

完成 Task 9–14。此时可以声称 ICE/DTLS/SRTP/RTP/RTCP/TWCC/GoogCC/pacer/FEC 的组件链路成立。

### Checkpoint D：三进程 localhost 原型

完成 Task 15–17。此时才可以声称本机真实网络环回 pipeline 打通。

### Checkpoint E：可做研究对照实验

完成 Task 18–19。此时才具备比较“无 FEC / 统一 FEC / 语义分块 FEC / 后续 UEP”的可靠实验基础。

## 10. 后续对照实验，但不混入第一版实现

第一版稳定后，再用同一测试框架增加以下可切换策略：

1. 无 FEC；
2. 全部区域统一 FEC；
3. 本计划的 per-block P0/P1/P2 FEC；
4. WebRTC `num_important_packets/use_unequal_protection` 真 UEP；
5. 理想 oracle 策略；
6. 可选 P0-only、deadline-aware RTX。

这些策略必须共享相同的生成数据、网络 seed、码率预算、deadline 和指标定义，否则比较没有研究意义。

## 11. 最终交付清单

- 可复现构建的三个 exe；
- 明确版本的 OCTREE-SIM wire format；
- 实际生成和恢复的 native ULPFEC repair packets；
- 真实 PeerConnection/ICE/DTLS/SRTP/RTP/RTCP localhost 链路；
- TWCC/GoogCC → `SetRates` → semantic admission 的闭环；
- WebRTC pacer 发送与可观测队列延迟；
- P0/P1/P2 独立接收可用性和 deadline drop；
- 完整单元、组件、集成、E2E、弱网和 soak 测试；
- JSONL/CSV 指标、run manifest、补丁 manifest 和操作文档；
- 对每个能力都能指出对应自动化测试和运行证据。

计划执行顺序固定为 Task 0 → 19；只有同一 Task 内彼此独立的测试文件可以并行开发。任何跨 checkpoint 的捷径都不能作为完成证明。
