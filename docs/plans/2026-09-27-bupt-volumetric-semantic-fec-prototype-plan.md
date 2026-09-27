# BUPT Volumetric Semantic-FEC Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 构建一个双端体积视频传输原型：发送端按固定规则生成并“编码”每帧 2,000,000 字节的模拟八叉树点云数据，按 P0/P1/P2 语义等级分块，通过 WebRTC 原生网络栈发送；接收端不渲染，只输出块恢复、区域可用性、拥塞控制、FEC、队列和端到端延迟等可复现实验数据。

**Architecture:** 产品代码位于独立工程 `D:\BUPT-XR\bupt_volumetric`，复用 `bupt_pc` 的双端、信令、配置、日志和延迟测量思想，但不直接复制其媒体实现。媒体面使用自定义体积视频 RTP 负载；WebRTC 负责 ICE/DTLS/SRTP、TWCC/GoogCC、RTCP、pacer 和原生 FEC 编解码，应用层负责语义映射、预算和区域生命周期。对 vendored libwebrtc 只做一组边界清晰、带上游单测的最小补丁。全部产品测试位于根目录 `D:\BUPT-XR\test\test_volumetric_*`。

**Tech Stack:** C++20、CMake/Ninja、Windows x64 clang-cl、固定版本原生 libwebrtc、GoogleTest/GoogleMock、PowerShell、JSONL/CSV 指标文件。

---

## 0. 文档状态与强约束

本文件是实施计划，不是实现。初次落盘时只创建了计划目录和本文件。2026-09-27 的后续骨架阶段创建了计划所列的应用、文档、配置、脚本和测试文件，但其中 `.h/.cc/.ps1/CMakeLists.txt` 仍只有职责与测试意图注释，不包含功能实现；`extern/webrtc/src` 仍未修改。骨架清单见 `docs/scaffold-manifest.md`。

实施时必须遵守：

- 不把 `D:\BUPT-XR` 当作 Git 仓库；它当前不是 Git 仓库。
- 不对 `extern\webrtc\src` 执行 `git reset --hard`、`git checkout --`、`git clean`、批量换行转换或大范围同步。
- 修改 WebRTC 前先完整阅读 `D:\BUPT-XR\extern\webrtc\src\GEMINI.md`。
- 不运行例行 `gclient sync`，不重建整个 `out\Default`，不升级 WebRTC。
- 新的应用代码全部放在 `bupt_volumetric`，上游目录只允许出现本计划列明的最小集成补丁。
- 每个功能遵循“失败测试 → 最小实现 → 测试通过 → 重构 → 回归”的顺序。
- 测试脚手架、跳过测试、只检查进程启动但不检查结果，均不能作为功能验收。
- 性能测试与正确性测试分开；性能结果必须带机器、网络、配置和时钟不确定度，不能只报一个平均数。
- 第一版不宣称是 G-PCC/V3C 编解码器；它只是具有八叉树风格层级与语义区域的确定性模拟数据生成器。

## 1. 第一版范围

### 1.1 必须交付

- 一个信令服务器进程 `bupt_volumetric_signaling_server.exe`。
- 一个发送端进程 `bupt_volumetric_sender.exe`。
- 一个接收端进程 `bupt_volumetric_receiver.exe`。
- 回环、本机双进程和两台主机三种运行方式。
- 可配置的单次发送、1/5/10/15/30 fps 和自适应帧率模式。
- 每帧精确生成 2,000,000 字节模拟编码数据。
- P0/P1/P2 分区、语义块、RTP 分片和按类保护。
- WebRTC 原生拥塞控制和 pacer；不绕过、不关闭 pacer。
- P0 50% FEC、P1 12.5% FEC、P2 无 FEC 且无 RTX。
- 带宽不足时按语义价值进行准入、降级和过期淘汰。
- 接收端支持在一帧不完整时独立确认已恢复/已到达的语义块。
- 单机单调时钟分段延迟与跨机时钟校准后的单向延迟。
- 控制台摘要、JSONL 原始事件、CSV 聚合结果。
- 可确定复现的随机丢包、突发丢包、抖动、重排、限速和断网回归矩阵。

### 1.2 第一版明确不做

- 真实摄像头、真实点云采集或点云渲染。
- 真正的 G-PCC、V-PCC、Draco、八叉树熵编码或 GPU 编解码。
- XR 头显显示、MTP 或玻璃到玻璃延迟。
- 音频、双向媒体和多人房间。
- 应用层 Reed-Solomon/RaptorQ；第一版以 WebRTC 原生 FEC 为主，应用层只做映射和策略。
- 把 DataChannel 用作 2 MB/帧的主媒体通道。
- 通过禁用 pacer 制造只适用于本机回环的低延迟数字。
- 第一版直接承诺“跨公网低于 10 ms”；先建立可解释、可复现的测量基线。

## 2. 已敲定的数据与保护模型

### 2.1 一帧的固定组成

“2 MB”在本工程中固定解释为十进制 `2,000,000 bytes`：

| 等级 | 占比 | 源数据字节 | 含义 | 默认保护 |
|---|---:|---:|---|---:|
| P0 | 1/8 | 250,000 | 全局骨架、根/浅层八叉树、最重要语义区域 | 50% FEC |
| P1 | 3/8 | 750,000 | 主要表面和中层细节 | 12.5% FEC |
| P2 | 4/8 | 1,000,000 | 末级细节、低价值区域 | 无 FEC、无 RTX |

理论 FEC 字节预算：

- P0 repair：125,000 B。
- P1 repair：93,750 B。
- P2 repair：0 B。
- 总 repair：218,750 B，占源数据 10.9375%。
- 不含 RTP/UDP/IP/SRTP/RTCP 的理论媒体总量：2,218,750 B/帧。

### 2.2 分块和包参数

- 目标 RTP 媒体负载：1,150 B；最终值由路径 MTU 和 SRTP 扩展头预算验证。
- 一个标准 FEC block 含 `K=32` 个 source symbols。
- P0 标准块：32 source + 16 repair，修复率 50%，初始映射到 WebRTC `fec_rate≈128/255`。
- P1 标准块：32 source + 4 repair，修复率 12.5%，初始映射到 `fec_rate≈32/255`。
- P2：仅 source symbols。
- 尾块按实际 source symbol 数计算 repair 数，采用确定性向上取整并记录 padding；不能默默少保护。
- `max_fec_frames=1`，禁止跨点云帧累积 FEC。
- 初始 FEC mask 采用 WebRTC random mask；bursty mask 作为弱网对照项。
- 每个块必须有唯一 `(session_id, frame_id, semantic_class, region_id, block_id)`。

### 2.3 不使用“关键帧/时间层”冒充语义优先级

P0/P1/P2 是点云空间/语义优先级，不是视频关键帧、delta frame 或 temporal layer。实现不得借用这些字段改变含义，否则会污染 WebRTC 自身的帧依赖、码率分配和统计。

## 3. 端到端系统结构

```text
sender
  deterministic simulated octree generator
    -> semantic partitioner (P0/P1/P2, regions)
    -> block planner (K=32, deadline, dependency)
    -> semantic admission controller
    -> custom volumetric RTP packetizer
    -> native WebRTC FEC/TWCC/GoogCC/pacer/ICE/DTLS/SRTP
                        network
    <- RTCP/TWCC/loss/RTT/BWE -----------------------------

receiver
  WebRTC SRTP/RTP/FEC receive path
    -> post-FEC semantic packet observer
    -> block assembler and integrity verifier
    -> partial frame/region availability tracker
    -> metrics and latency recorder
    -> console + JSONL + CSV (no renderer)
```

### 3.1 控制面

- HTTP long-polling 信令只传 SDP/ICE 和会话加入/离开消息，结构参考 `bupt_pc`，代码独立维护。
- PeerConnection 建立后创建可靠有序 DataChannel，仅传运行配置、能力协商、开始/停止、时钟同步探测和结果确认。
- 媒体数据绝不经该 DataChannel。
- RTCP/TWCC 是网络闭环主反馈；DataChannel 不建立第二套拥塞控制器。

### 3.2 媒体面

- 注册专用 codec 名称 `OCTREE-SIM/90000`。
- RTP timestamp 使用 90 kHz；同一点云帧共享逻辑 `frame_id`，不同语义块可独立完成和交付。
- 自定义负载头包含版本、frame id、class、region、block、symbol index、source/repair、原始长度和 deadline delta。
- WebRTC 仍负责传输目标码率、pacer 发送、TWCC 和安全传输。
- 应用策略只决定“哪些 source block 有资格进入媒体队列”和“每个已准入块的保护档位”。

### 3.3 为什么需要有限的 WebRTC 补丁

stock Generic Video 路径按完整视频帧组装。若 P2 任意包缺失，整帧可能无法交给上层，这与“P0/P1 已完整即可独立使用”的研究目标冲突。同时，现有 `UlpfecGenerator` 不暴露语义块边界，且其 UEP 参数没有从应用策略贯通。因此第一版采用显式的小补丁，而不是假装只改几个参数即可实现语义 FEC。

补丁边界固定为：

1. 注册 `OCTREE-SIM` 的 packetizer/depacketizer。
2. 给发送 RTP 包携带进程内 semantic class/block metadata；这些 metadata 不依赖视频关键帧字段。
3. 给 ULPFEC generator 增加显式 `Begin/Add/EndSemanticFecBlock` 生命周期或等价接口，确保 P2 不受保护时也不会阻止 P0/P1 block flush。
4. 在 SRTP 解密和 FEC 恢复之后、传统视频完整帧 buffer 之前，增加可选 semantic payload observer。
5. 暴露 recovered source packet 和丢弃 repair packet 的准确计数。

不得扩展为重写 GoogCC、pacer、ICE、DTLS、SRTP 或 PeerConnection。

## 4. 拥塞控制和发送策略

### 4.1 单一控制权

- GoogCC/TWCC 决定 transport target bitrate。
- WebRTC pacer 决定实际出队时间。
- FEC controller/保护策略从 transport budget 中预留 repair 开销。
- Semantic Admission Controller 决定 P0/P1/P2 source blocks 的准入和 LOD。
- WebRTC stats 用于观测和慢速策略输入，不作为每包控制回路。

应用层不得再实现一个与 GoogCC 相互抢占的 AIMD/GCC。

### 4.2 预算口径

每个控制周期必须同时记录：

- `transport_target_bps`：WebRTC 给出的总发送目标。
- `protocol_reserve_bps`：RTP 扩展、SRTP、UDP/IP、RTCP 和估算误差预留。
- `media_plus_fec_budget_bps`：可用于 source + repair 的预算。
- `fec_budget_bps`：按已准入块和保护档位计算。
- `source_budget_bps`：扣除 FEC 后可发送的源数据。
- `actual_pacer_exit_bps`：实际从 pacer 发送的速率。

初始协议预留系数为 10%，但以实测线上字节校正。不能既先乘 0.9 又把同一份 FEC/协议开销重复扣除。

### 4.3 固定帧的理论门槛

不含网络头的完整质量需求为：

| fps | 媒体+FEC 速率 |
|---:|---:|
| 1 | 17.75 Mbps |
| 5 | 88.75 Mbps |
| 10 | 177.50 Mbps |
| 15 | 266.25 Mbps |
| 30 | 532.50 Mbps |

实际链路需求必然更高。默认演示模式采用 5 fps；1 fps 用于正确性，10/15/30 fps 用于容量和退化实验。

### 4.4 准入与过期顺序

发送价值严格排序：

1. 新帧 P0。
2. 当前帧 P1。
3. 当前帧 P2。
4. 旧帧任何尚未发送的数据。

当下一帧产生时：

- 旧 P2 未入 pacer 的包立即淘汰。
- 旧 P1 超过 deadline 后淘汰。
- 旧帧不得阻塞新 P0。
- 已进入 WebRTC 内部、无法安全撤回的包必须计入 `late_or_stale_bytes`，用于反推更早的准入截止点。

每帧预算决策：

- `>=2,218,750 B`：发送完整 P0/P1/P2。
- `>=1,218,750 B` 且低于完整量：完整 P0+FEC、完整 P1+FEC，剩余预算给完整 P2 blocks。
- `>=375,000 B` 且低于上述门槛：完整 P0+FEC，然后发送能容纳的完整 P1 blocks，不发 P2。
- `<375,000 B`：降低模拟八叉树 P0 深度、降帧率或跳帧；禁止长期排队一个无法完成的 P0。

### 4.5 防振荡

- 带宽上升需连续 5 个控制周期满足升级阈值。
- 带宽下降连续 2 个周期即可降级。
- FEC 档位最短保持 500 ms。
- 帧率最短保持 2 s。
- 所有阈值和保持时间进入配置与指标，不写成不可观测的魔数。

## 5. 延迟与时钟方案

### 5.1 同机或单进程内阶段

全部使用单调时钟，禁止用系统墙钟计算 duration：

- simulate encode duration；
- partition duration；
- admission wait；
- app queue wait；
- packetization duration；
- WebRTC/pacer send delay；
- network receive 到 block usable；
- FEC recovery delay；
- receiver assembly duration。

### 5.2 跨主机单向延迟

DataChannel 运行 NTP-style 四时间戳探测：`t0` 发送、`t1` 接收、`t2` 回复、`t3` 收到。采用最小 RTT 样本窗口估计 offset，持续跟踪 drift，并为每个单向延迟输出 uncertainty。

规则：

- 未收敛时只报告 RTT 和本地分段，不报告伪精确单向延迟。
- 单向延迟结果必须同时输出 `estimate_us` 和 `uncertainty_us`。
- offset 跳变超过阈值时开启新的 clock epoch，禁止跨 epoch 相减。
- 研究级两机基线优先使用有线 PTP；无 PTP 时明确标记为软件估计。

### 5.3 延迟事件

至少记录：

- `frame_generated`
- `partition_completed`
- `block_admitted`
- `source_packet_enqueued`
- `pacer_exit`
- `packet_received`
- `fec_packet_received`
- `source_packet_recovered`
- `block_usable`
- `p0_usable`
- `p1_usable`
- `frame_deadline`
- `frame_closed`

报告 P50/P90/P95/P99/max、样本数、丢失率和时钟不确定度，平均数仅作补充。

## 6. 指标定义

### 6.1 发送端

- generated/admitted/dropped frames and blocks by class。
- source/repair/header/RTCP bytes。
- target/source/FEC/actual bitrate。
- app queue depth and age。
- pacer queue delay/bytes。
- stale bytes、deadline drops、budget drops。
- RTT、TWCC loss、available outgoing bitrate。

### 6.2 接收端

- received source/repair packets by class。
- recovered source packets by class/block。
- unrecovered symbols、duplicate、late、reordered packets。
- block integrity pass/fail。
- P0/P1/P2 available bytes and completeness。
- `p0_usable`, `p0_p1_usable`, `full_frame_usable` rates。
- partial frame quality score：`1.0*P0 + configurable_weight*P1/P2`，权重写入结果文件。

### 6.3 WebRTC 原生指标解释

- `totalPacketSendDelay` 表示 packetizer 到 OS socket 的累计等待，不等于端到端延迟。
- `totalAssemblyTime` 表示多包帧首包到末包的组装时间，不等于 jitter buffer 延迟。
- `jitterBufferDelay/TargetDelay/MinimumDelay` 仅在对应接收路径存在时解释。
- `fecPacketsReceived/fecPacketsDiscarded` 不能当作恢复成功数；恢复成功必须来自 `DecodeFecResult.num_recovered_packets` 或等价准确埋点。

### 6.4 文件格式

- `events.jsonl`：每行一个强类型事件，保留原始时间戳。
- `summary.csv`：一行一个 scenario/run，便于批量分析。
- `run_manifest.json`：提交版本、WebRTC commit、构建参数、机器、NIC、网络规则、配置 hash、随机种子。
- 任何 schema 变更必须增加 `schema_version` 并有兼容性测试。

## 7. 目标目录结构

```text
D:\BUPT-XR\bupt_volumetric\
  CMakeLists.txt
  CMakePresets.json
  README.md
  cmake\
    ClangClToolchain.cmake
    CompilerOptions.cmake
    GoogleTestImport.cmake
    WebRtcDependency.cmake
  config\
    sender.example.ini
    receiver.example.ini
    signaling.example.ini
    scenarios.example.json
  docs\
    architecture\
      overview.md
      wire-format.md
      semantic-protection.md
      congestion-control.md
      latency-and-metrics.md
      webrtc-patch-boundary.md
    operations\
      build.md
      run-loopback.md
      run-two-pcs.md
      run-network-matrix.md
    plans\
      2026-09-27-bupt-volumetric-semantic-fec-prototype-plan.md
  include\bupt_volumetric\
    base\
    config\
    model\
    simulation\
    protection\
    transport\
    receiver\
    latency\
    metrics\
    rtc\
    signaling\
  src\
    base\
    config\
    model\
    simulation\
    protection\
    transport\
    receiver\
    latency\
    metrics\
    rtc\
    signaling\
    apps\
  scripts\
    configure.ps1
    build.ps1
    run_loopback.ps1
    run_network_matrix.ps1
    collect_results.ps1
  packaging\
    runtime_manifest.json
  webrtc_patches\
    README.md
    manifest.json

D:\BUPT-XR\test\
  test_volumetric_build_contract\
  test_volumetric_model\
  test_volumetric_packetization\
  test_volumetric_protection\
  test_volumetric_congestion\
  test_volumetric_receiver\
  test_volumetric_latency\
  test_volumetric_metrics\
  test_volumetric_signaling\
  test_volumetric_rtc\
  test_volumetric_loopback_e2e\
  test_volumetric_network_resilience\
  test_volumetric_two_pc_e2e\
  test_volumetric_soak\
  test_volumetric_packaging\
```

## 8. 接口先行约定

实施前先定义并冻结以下职责，不把实现细节泄漏到应用入口：

| 接口 | 输入 | 输出/职责 |
|---|---|---|
| `SimulatedFrameGenerator` | frame id、seed、2,000,000 B profile | 可重复验证的模拟编码帧 |
| `SemanticPartitioner` | 模拟帧 | 精确 250k/750k/1000k 的区域集合 |
| `BlockPlanner` | 区域、MTU、K | source block 和 symbol descriptor |
| `ProtectionPolicy` | class、loss、RTT、budget | FEC rate/mask/deadline，不做拥塞控制 |
| `TransportBudgetAllocator` | transport target、fps、overhead model | source/FEC/class budgets |
| `SemanticAdmissionController` | blocks、budget、queue state、deadline | admit/drop/defer 决策和原因 |
| `SemanticPacketizer` | admitted block | 可线传且可恢复的 RTP payload records |
| `TransportFeedbackSnapshot` | WebRTC feedback | 单一口径的 BWE/RTT/loss/pacer 状态 |
| `SemanticPacketObserver` | post-SRTP/post-FEC packet | packet metadata 和 recovered 标志 |
| `BlockAssembler` | source/recovered symbols | block usable/incomplete/corrupt 状态 |
| `FrameAvailabilityTracker` | block state + deadline | P0/P1/P2/frame 结果 |
| `ClockSyncEstimator` | 四时间戳样本 | offset、drift、uncertainty、epoch |
| `LatencyRecorder` | 强类型事件 | local/cross-host latency samples |
| `MetricsSink` | metrics/events | console、JSONL、CSV |

所有接口使用显式字节、比特率和时间单位；类型名或字段名必须包含 `_bytes`、`_bps`、`_us` 等单位后缀。

## 9. 实施任务

### Task 1：建立可独立配置的工程和测试入口

**Files:**

- Create: `bupt_volumetric/CMakeLists.txt`
- Create: `bupt_volumetric/CMakePresets.json`
- Create: `bupt_volumetric/cmake/ClangClToolchain.cmake`
- Create: `bupt_volumetric/cmake/CompilerOptions.cmake`
- Create: `bupt_volumetric/cmake/GoogleTestImport.cmake`
- Create: `bupt_volumetric/cmake/WebRtcDependency.cmake`
- Modify: `test/CMakeLists.txt`
- Create: `test/test_volumetric_build_contract/CMakeLists.txt`
- Create: `test/test_volumetric_build_contract/build_contract_test.cc`

- [ ] 先写失败测试：检查 C++20、x64 clang-cl、`WEBRTC_ROOT`、`WEBRTC_OUT_DIR`、WebRTC commit 和库文件来源。
- [ ] 添加一个只链接最小库的 smoke target，证明不是复制旧 exe。
- [ ] 为 volumetric tests 增加独立 helper，不能依赖 `bupt_pc_*` target。
- [ ] 配置输出固定为 `D:\BUPT-XR\out\bupt_volumetric\clang-release`。
- [ ] 运行：`cmake --preset windows-clang-release`。
- [ ] 运行：`cmake --build --preset windows-clang-release --target test_volumetric_build_contract`。
- [ ] 运行：`ctest --preset windows-clang-release -R volumetric_build_contract --output-on-failure`。
- [ ] 预期：测试通过，并打印实际 WebRTC checkout、commit 和 `webrtc.lib` 路径。
- [ ] Checkpoint：记录配置日志；若未来该工程单独初始化 Git，再提交该任务。

### Task 2：固定配置、错误模型和运行清单

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/config/app_config.h`
- Create: `bupt_volumetric/src/config/app_config.cc`
- Create: `bupt_volumetric/include/bupt_volumetric/base/result.h`
- Create: `bupt_volumetric/config/*.example.ini`
- Create: `bupt_volumetric/config/scenarios.example.json`
- Create: `test/test_volumetric_model/config_test.cc`

- [ ] 测试缺失、非法和边界配置：负 fps、错误占比、过大 MTU、FEC 超界、重复端口。
- [ ] 配置默认值固定：2,000,000 B、P0 1/8、P1 3/8、P2 4/8、1150 B、K=32。
- [ ] 启动时将完整有效配置和 hash 写入 run manifest。
- [ ] 预期：无效配置在建立网络前失败，并指出字段名和实际值。

### Task 3：确定性模拟八叉树帧和语义分区

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/model/simulated_octree_frame.h`
- Create: `bupt_volumetric/include/bupt_volumetric/model/semantic_region.h`
- Create: `bupt_volumetric/include/bupt_volumetric/simulation/frame_generator.h`
- Create: `bupt_volumetric/include/bupt_volumetric/simulation/semantic_partitioner.h`
- Create: corresponding `src/model/*` and `src/simulation/*`
- Create: `test/test_volumetric_model/frame_generator_test.cc`
- Create: `test/test_volumetric_model/semantic_partitioner_test.cc`

- [ ] 先测试相同 seed/frame id 逐字节一致，不同 frame id 内容不同。
- [ ] 测试每帧精确 2,000,000 B，无隐藏容器开销计入 payload。
- [ ] 测试 P0/P1/P2 精确为 250,000/750,000/1,000,000 B。
- [ ] 每个 region 生成可校验 pattern 和 CRC；故意损坏必须被发现。
- [ ] 模拟层级、region 数和语义标签进入 payload metadata，但 README 明示不是真实 codec。
- [ ] 增加 10,000 帧属性测试，检查不越界、不重复、不漏字节。

### Task 4：线格式和分片器

**Files:**

- Create: `bupt_volumetric/docs/architecture/wire-format.md`
- Create: `bupt_volumetric/include/bupt_volumetric/transport/wire_header.h`
- Create: `bupt_volumetric/include/bupt_volumetric/transport/block_planner.h`
- Create: `bupt_volumetric/include/bupt_volumetric/transport/semantic_packetizer.h`
- Create: corresponding `src/transport/*`
- Create: `test/test_volumetric_packetization/wire_header_test.cc`
- Create: `test/test_volumetric_packetization/block_planner_test.cc`
- Create: `test/test_volumetric_packetization/packetizer_roundtrip_test.cc`

- [ ] 先冻结 versioned network-byte-order header；未知版本必须拒绝。
- [ ] 测试 encode/decode round trip、截断、非法长度、整数溢出和重复 symbol。
- [ ] 测试标准块 K=32 和 P0/P1/P2 尾块数量。
- [ ] 测试每个 RTP payload 不超过配置上限。
- [ ] fuzz 风格循环测试任意截断点，不能崩溃或越界。
- [ ] Golden fixtures 存入 `test/fixtures/volumetric_protocol/` 并带 schema version。

### Task 5：FEC 策略、预算和恢复的纯算法基线

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/protection/fec_profile.h`
- Create: `bupt_volumetric/include/bupt_volumetric/protection/protection_policy.h`
- Create: `bupt_volumetric/include/bupt_volumetric/protection/protection_budget.h`
- Create: corresponding `src/protection/*`
- Create: `test/test_volumetric_protection/fec_profile_test.cc`
- Create: `test/test_volumetric_protection/protection_budget_test.cc`
- Create: `test/test_volumetric_protection/deterministic_loss_test.cc`

- [ ] 测试 P0 标准块 16 repair、P1 标准块 4 repair、P2 0 repair。
- [ ] 测试尾块 rounding 和总理论 repair 预算 218,750 B 的允许 padding 误差。
- [ ] 使用 WebRTC FEC 单元接口做确定性擦除恢复测试，而不是自造生产 FEC。
- [ ] 对每个块注入 0..R 个可恢复模式以及 R+1 个不可恢复模式。
- [ ] 同时覆盖 random 和 bursty mask；记录 bursty mask 的 12 包有效保护限制。
- [ ] 验证恢复数来自 decoder 结果而不是 repair 包接收数。

### Task 6：带宽预算与语义准入控制器

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/transport/transport_budget.h`
- Create: `bupt_volumetric/include/bupt_volumetric/transport/semantic_admission_controller.h`
- Create: corresponding `src/transport/*`
- Create: `test/test_volumetric_congestion/budget_allocator_test.cc`
- Create: `test/test_volumetric_congestion/admission_controller_test.cc`
- Create: `test/test_volumetric_congestion/hysteresis_test.cc`

- [ ] 表驱动测试完整、P0+P1、仅 P0 和不足 P0 四个预算区间。
- [ ] 测试协议/FEC 开销只扣除一次。
- [ ] 测试不发送半个必须原子交付的 block。
- [ ] 测试新帧到达立即淘汰旧 P2、deadline 后淘汰旧 P1。
- [ ] 测试升档 5 周期、降档 2 周期、FEC 500 ms、fps 2 s 的防振荡规则。
- [ ] 生成每个 drop 的明确 reason code。

### Task 7：接收端块组装和残缺帧可用性

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/receiver/block_assembler.h`
- Create: `bupt_volumetric/include/bupt_volumetric/receiver/frame_availability_tracker.h`
- Create: corresponding `src/receiver/*`
- Create: `test/test_volumetric_receiver/block_assembler_test.cc`
- Create: `test/test_volumetric_receiver/partial_frame_test.cc`
- Create: `test/test_volumetric_receiver/deadline_test.cc`

- [ ] 先测试乱序、重复、丢包、FEC 恢复包、损坏包和跨帧混入。
- [ ] P0 完整时立即产生一次 `p0_usable`，不等待 P1/P2。
- [ ] P1 完整时更新质量，P2 缺失不撤销 P0/P1 可用状态。
- [ ] deadline 到达后关闭帧并清理内存；迟到包计数但不复活帧。
- [ ] 为 active frames/blocks 设置硬上限，超限采用最旧低优先级淘汰。
- [ ] 在 AddressSanitizer 可用配置下运行恶意长度和超量包测试。

### Task 8：WebRTC 传输接缝可行性门禁

**Files:**

- Create: `bupt_volumetric/docs/architecture/webrtc-patch-boundary.md`
- Create: `bupt_volumetric/webrtc_patches/README.md`
- Create: `bupt_volumetric/webrtc_patches/manifest.json`
- Create: `test/test_volumetric_rtc/transport_seam_contract_test.cc`
- Inspect only first: relevant files under `extern/webrtc/src/api/video`, `modules/rtp_rtcp/source`, and `video/`.

- [ ] 阅读上游 `GEMINI.md`，记录可修改规则。
- [ ] 写失败 contract test，证明 stock Generic Video 在 P2 丢包时不能满足独立 P0 交付，防止错误路线继续推进。
- [ ] 证明自定义 codec 可协商 `OCTREE-SIM/90000` 且 90 kHz timestamp 不被改写。
- [ ] 证明 semantic observer 能看到普通 source 和 FEC recovered source，并区分 recovered 标志。
- [ ] 证明每个 semantic FEC block 会独立 flush；P2 最后一包不受保护时，P0/P1 repair 仍已生成。
- [ ] 只有四项全部通过才进入 Task 9；失败时停止实现并更新此计划，不允许退回 DataChannel 主媒体或整帧 Generic 假实现。

### Task 9：实施并单测最小 WebRTC 补丁

**Candidate upstream files, exact set finalized by Task 8:**

- Modify: `extern/webrtc/src/api/video/video_codec_type.h`
- Create: `extern/webrtc/src/modules/rtp_rtcp/source/rtp_format_octree_sim.{h,cc}`
- Create: `extern/webrtc/src/modules/rtp_rtcp/source/video_rtp_depacketizer_octree_sim.{h,cc}`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/rtp_format.cc`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/create_video_rtp_depacketizer.cc`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/rtp_packet_to_send.{h,cc}`
- Modify: `extern/webrtc/src/modules/rtp_rtcp/source/ulpfec_generator.{h,cc}`
- Modify: `extern/webrtc/src/video/rtp_video_stream_receiver2.{h,cc}`
- Modify: nearest scoped `BUILD.gn` files only.
- Add/modify: corresponding upstream `*_unittest.cc` files.

- [ ] 每个补丁先加上游单测，再改生产代码。
- [ ] semantic metadata 仅为进程内强类型数据；线上字段由专用 payload header 承载。
- [ ] block lifecycle 能强制 flush，且不依赖整帧 RTP marker。
- [ ] UEP 参数显式贯通，默认路径保持原行为。
- [ ] observer 默认关闭，关闭时不改变现有视频行为和 ABI 可见语义。
- [ ] 恢复计数严格来自 decode result。
- [ ] 运行最窄 Ninja targets 和相关单测，记录命令与结果。
- [ ] 运行现有 generic packetizer、ULPFEC、RTP receiver 回归测试。
- [ ] 用 `git -c core.safecrlf=false diff --ignore-cr-at-eol` 审核有意义差异。
- [ ] 将每个补丁文件 hash、上游 commit 和测试 target 写入 patch manifest。

### Task 10：模拟编码适配器和发送端媒体管线

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder.h`
- Create: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_encoder_factory.h`
- Create: `bupt_volumetric/include/bupt_volumetric/rtc/peer_connection_session.h`
- Create: corresponding `src/rtc/*`
- Create: `test/test_volumetric_rtc/encoder_factory_test.cc`
- Create: `test/test_volumetric_rtc/sender_pipeline_test.cc`

- [ ] encoder 忽略占位视频像素，只从 generator 获取模拟帧；命名和日志不得声称真实编码。
- [ ] WebRTC rate callback 更新 budget snapshot，不直接钉死码率。
- [ ] 每次发送决策生成 source/FEC/class budget event。
- [ ] 发送端不得无限缓存；测试 bounded queue 和 stale eviction。
- [ ] 验证 1/5/10/15/30 fps 下生成速率与实际准入速率分开统计。

### Task 11：接收端 WebRTC 适配和结果输出

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/rtc/semantic_payload_observer_adapter.h`
- Create: `bupt_volumetric/src/rtc/semantic_payload_observer_adapter.cc`
- Create: `bupt_volumetric/include/bupt_volumetric/rtc/octree_sim_decoder.h`
- Create: `bupt_volumetric/src/rtc/octree_sim_decoder.cc`
- Create: `test/test_volumetric_rtc/receiver_pipeline_test.cc`

- [ ] post-FEC observer 驱动 BlockAssembler，不依赖完整视频帧回调。
- [ ] decoder 只维持 WebRTC codec 生命周期，不渲染；不可伪造恢复成功。
- [ ] 测试 P2 丢失时 P0/P1 仍然产生结果。
- [ ] 测试 receiver 重启、SSRC 变化和 frame id wrap/epoch。

### Task 12：信令、控制 DataChannel 和应用入口

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/signaling/*`
- Create: `bupt_volumetric/src/signaling/*`
- Create: `bupt_volumetric/src/apps/signaling_server_main.cc`
- Create: `bupt_volumetric/src/apps/sender_main.cc`
- Create: `bupt_volumetric/src/apps/receiver_main.cc`
- Create: `test/test_volumetric_signaling/*_test.cc`
- Create: `test/test_volumetric_rtc/control_channel_test.cc`

- [ ] 参考 `bupt_pc` 协议概念，但新 namespace、新 target、新测试，不复制陈旧 README 声明。
- [ ] 测试 SDP/ICE framing、部分读写、超长消息、断线重连、错误 peer id。
- [ ] DataChannel 能力协商必须校验 wire version、frame profile、FEC capabilities。
- [ ] 收发端状态机对重复 start/stop、对端先退出和信令重连幂等。
- [ ] CLI `--help` 和无效配置不依赖网络即可测试。

### Task 13：时钟同步、延迟和指标系统

**Files:**

- Create: `bupt_volumetric/include/bupt_volumetric/latency/*`
- Create: `bupt_volumetric/src/latency/*`
- Create: `bupt_volumetric/include/bupt_volumetric/metrics/*`
- Create: `bupt_volumetric/src/metrics/*`
- Create: `test/test_volumetric_latency/*_test.cc`
- Create: `test/test_volumetric_metrics/*_test.cc`

- [ ] 移植 `bupt_pc` 的成熟思想前先写 contract tests，不直接复制未知假设。
- [ ] fake clock 测试 offset、drift、RTT asymmetry、epoch reset、样本淘汰。
- [ ] 测试 local duration 永远只用 monotonic clock。
- [ ] JSONL schema golden test、CSV escaping、崩溃后可读取的 flush 行为。
- [ ] 并发事件写入不丢失、不乱用墙钟相减。
- [ ] 汇总器对空样本、单样本和极端值给出明确结果。

### Task 14：无网络算法端到端测试

**Files:**

- Create: `test/test_volumetric_loopback_e2e/in_process_pipeline_test.cc`
- Create: `test/test_volumetric_loopback_e2e/deterministic_channel.h`

- [ ] 不启动 PeerConnection，串起生成、分区、预算、packetize、loss、FEC、assembly。
- [ ] 固定 seed 覆盖零丢包、P0 可恢复、P1 可恢复、超过修复能力和 P2 丢包。
- [ ] 断言实际字节、恢复数、质量结果和 reason codes，而非只断言“不崩溃”。
- [ ] 运行 10,000 帧并检查内存上限、无 frame state 泄漏。

### Task 15：真实 WebRTC 本机回环 E2E

**Files:**

- Create: `bupt_volumetric/scripts/run_loopback.ps1`
- Create: `test/test_volumetric_loopback_e2e/loopback_process_test.ps1`
- Create: `test/test_volumetric_loopback_e2e/validate_results.py` only if bundled Python is selected; otherwise C++ validator.

- [ ] 启动信令、receiver、sender，使用动态空闲端口和独立临时结果目录。
- [ ] 单帧测试必须验证 2,000,000 B source、P0/P1/P2 和 FEC 计数。
- [ ] 1 fps 运行 30 帧做正确性基线。
- [ ] 5 fps 运行 60 s 做默认演示。
- [ ] 10/15/30 fps 记录容量退化，不要求全部完整；必须优先保 P0。
- [ ] 验证 pacer/TWCC/RTT 指标非空，不能以固定假数据通过。
- [ ] 每个进程设置 timeout，失败时保留日志和 run manifest。

### Task 16：弱网回归矩阵

**Files:**

- Create: `bupt_volumetric/scripts/run_network_matrix.ps1`
- Create: `test/test_volumetric_network_resilience/scenario_matrix.json`
- Create: `test/test_volumetric_network_resilience/network_matrix_test.ps1`
- Create: `test/test_volumetric_network_resilience/result_assertions.*`

- [ ] 优先使用 WebRTC 可控 fake network 做 CI 确定性回归。
- [ ] Windows 实网 shaping 作为手工/实验层，记录工具版本和规则。
- [ ] 矩阵至少覆盖 loss 0/1/3/5/10%，burst 2/4/8 包，RTT 5/20/50/100 ms，jitter 0/5/20 ms，重排 0/1/5%，容量阶跃和 500 ms outage。
- [ ] 每个 scenario 固定 seed 并至少重复 5 次。
- [ ] 断言 P0 可用率不低于无优先保护对照，且预算不足时 P2 先下降。
- [ ] 测试恢复收益和 FEC 开销；不能只证明“加 FEC 后字节更多”。
- [ ] 建立五组对照：无 FEC、统一 FEC、semantic FEC、native UEP、oracle policy。

### Task 17：两台主机 E2E 与时钟校准

**Files:**

- Create: `bupt_volumetric/docs/operations/run-two-pcs.md`
- Create: `test/test_volumetric_two_pc_e2e/two_pc_checklist.md`
- Create: `test/test_volumetric_two_pc_e2e/validate_two_pc_results.*`

- [ ] 先在同网段有线网络运行，再在跨网段/TURN 环境运行。
- [ ] 记录 candidate pair、relay/direct、路径 MTU、NIC、Wi-Fi/以太网和 TURN 区域。
- [ ] 时钟估计未收敛时验收脚本必须拒绝单向延迟结论。
- [ ] 输出 clock offset、drift、uncertainty 和 RTT 分布。
- [ ] 比较 direct 与 relay 的 P0/P1 可用率、pacer delay、assembly 和恢复收益。

### Task 18：长稳、资源和故障恢复

**Files:**

- Create: `test/test_volumetric_soak/soak_test.ps1`
- Create: `test/test_volumetric_soak/resource_assertions.*`

- [ ] 5 fps 连续运行至少 2 h；扩展实验 8 h。
- [ ] 监测 working set、handle、thread、queue、active frame state。
- [ ] 注入信令服务器重启、接收端重启、短断网和网络切换。
- [ ] 资源增长使用趋势阈值而不是单点硬编码。
- [ ] 恢复后新 P0 能在有限时间内再次到达；旧队列不能倾倒造成延迟尖峰。

### Task 19：文档、打包和最终验证

**Files:**

- Create: `bupt_volumetric/README.md`
- Create: `bupt_volumetric/docs/architecture/*.md`
- Create: `bupt_volumetric/docs/operations/*.md`
- Create: `bupt_volumetric/packaging/runtime_manifest.json`
- Create: `test/test_volumetric_packaging/*`

- [ ] README 明确“模拟数据、模拟编码、无渲染”，不得误导。
- [ ] 文档给出单帧字节账、完整命令、结果目录和指标解释。
- [ ] runtime manifest 只收集新生成且来源可证明的 exe/dll/config。
- [ ] 在干净输出目录完成 configure/build/test/package/smoke 全流程。
- [ ] 运行所有 `volumetric_*` 测试，再运行受 WebRTC 补丁影响的上游回归测试。
- [ ] 最终报告列出通过、失败、跳过和仅手工执行的测试；不得把 skip 计为 pass。

## 10. 测试分层和准入门禁

| 层级 | 运行频率 | 内容 | 失败后行为 |
|---|---|---|---|
| L0 静态/构建契约 | 每次构建 | 工具链、依赖来源、schema、target provenance | 停止 |
| L1 纯单元测试 | 每次改动 | model、packetizer、budget、policy、assembler、clock | 停止 |
| L2 WebRTC 上游单测 | 每次补丁改动 | packetizer/FEC/receiver 默认路径与新路径 | 停止 |
| L3 进程内确定性 E2E | 每次合入 | 固定 loss/reorder/burst 场景 | 停止 |
| L4 本机真实 WebRTC | 每日/里程碑 | ICE/DTLS/SRTP/TWCC/pacer/FEC | 停止发布 |
| L5 弱网矩阵 | 里程碑 | 多维网络条件和对照实验 | 输出回归差异 |
| L6 两机/跨网段 | 里程碑 | direct/relay、时钟校准 | 不满足则不做跨机延迟结论 |
| L7 soak | 发布候选 | 资源和恢复 | 停止发布 |

新发现的问题必须按以下顺序处理：

1. 先最小化成可重复场景。
2. 在对应 `test_volumetric_*` 中增加失败测试。
3. 再修改生产代码或 WebRTC 补丁。
4. 运行局部测试和受影响的完整回归。
5. 将 scenario、seed、根因和修复写进测试名或旁注文档。

## 11. 第一版验收标准

### 11.1 正确性

- 生成帧字节和三类占比 100% 精确。
- 无损场景所有 source blocks 通过完整性校验。
- P0/P1 标准块 FEC 计数与配置一致，P2 repair/RTX 为 0。
- P2 丢失不阻塞 P0/P1 可用事件。
- 所有 recovered counters 可与注入丢包逐包核对。
- 没有跨帧误组装、无限队列或 deadline 后复活。

### 11.2 网络闭环

- target bitrate 随可控容量变化，不再上下限钉死。
- loss、RTT、available bitrate、pacer queue 均有真实观测。
- 容量下降时，实测行为是 P2 → P1 → 降 P0 LOD/帧率，而不是队列无限增长。
- 容量恢复时按防振荡规则逐级恢复。

### 11.3 测量可信度

- 本地阶段只用单调时钟。
- 跨机单向延迟带 uncertainty；未收敛不输出误导数字。
- 报告包含 P50/P90/P95/P99/max 和样本数。
- run manifest 足以复现实验配置和随机场景。

### 11.4 工程质量

- 所有新测试位于 `D:\BUPT-XR\test\test_volumetric_*`。
- 所有新应用代码位于 `D:\BUPT-XR\bupt_volumetric`。
- WebRTC 补丁有 manifest、上游单测和默认路径回归。
- 不依赖旧二进制证明新代码工作。
- 构建、运行和结果验证命令可从文档复制执行。

## 12. 标准验证命令（实现完成后）

```powershell
Set-Location D:\BUPT-XR\bupt_volumetric
cmake --preset windows-clang-release
cmake --build --preset windows-clang-release
ctest --preset windows-clang-release --output-on-failure
```

WebRTC 补丁只构建 Task 8/9 确定的最窄 targets：

```powershell
Set-Location D:\BUPT-XR\extern\webrtc\src
.\third_party\ninja\ninja.exe -C out\Default <scoped-targets>
```

`<scoped-targets>` 不是可直接执行的占位验收命令；Task 8 必须根据实际 `BUILD.gn` target 写回确定列表后，才允许进入 Task 9。

## 13. 风险、止损点与研究价值

| 风险 | 早期检测 | 止损/处理 |
|---|---|---|
| stock video assembly 阻塞残缺区域 | Task 8 contract test | 使用限定 observer patch；不退回伪完整帧 |
| FEC block 未因 marker 触发 flush | block-boundary test | 显式 block lifecycle |
| repair 开销让拥塞更严重 | budget + matrix test | 降 P2/P1，再调 FEC；禁止越预算加冗余 |
| 多控制器振荡 | hysteresis test | 保持 GoogCC 单一传输控制权 |
| 跨机时钟误差掩盖延迟 | fake clock + uncertainty | 未收敛不报告单向值 |
| 2 MB×高 fps 超出网络/GPU/CPU | 10/15/30 fps capacity tests | 结果作为退化曲线，不伪称满质量成功 |
| 上游补丁扩大 | patch manifest/diff audit | 超出五类边界需重新评审计划 |
| 测试只验证进程存活 | result assertions | 必须验证字节、事件、恢复和优先级 |

该原型的研究贡献不是“给 WebRTC 调几个参数”，而是把点云语义价值、FEC 保护、拥塞预算、deadline 和部分可用性接到同一个可测闭环中，并与无 FEC、统一 FEC、原生 UEP 和 oracle 策略进行公平对照。

## 14. 推荐执行顺序与里程碑

- Milestone A（Task 1–7）：完全不碰网络，证明数据、分区、FEC 策略、预算和部分恢复正确。
- Milestone B（Task 8–9）：只打通并验证最小 WebRTC 接缝；这是继续投资的技术门禁。
- Milestone C（Task 10–13）：完成 sender/receiver/signaling、时钟与指标。
- Milestone D（Task 14–16）：完成本机 E2E 和可复现弱网矩阵。
- Milestone E（Task 17–19）：完成两机/跨网段、长稳、打包和最终证据。

每个里程碑结束时都必须提供：变更文件、精确构建命令、测试结果、未通过项、指标样例和下一阶段风险。没有测试证据，不进入下一里程碑。
