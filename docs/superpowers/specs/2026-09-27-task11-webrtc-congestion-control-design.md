# Task 11：模拟体积视频的 WebRTC 拥塞控制与观测设计

## 1. 目标

Task 11 要建立一条可验证的“假数据、假编解码、真传输控制”链路：

- 体积视频帧是确定性生成的模拟字节，不读取真实点云文件；
- OCTREE-SIM encoder/decoder 不执行八叉树压缩或解压；
- 不创建窗口、不渲染画面、不评价视觉质量；
- 媒体数据必须真实经过 PeerConnection、RTP、pacer、SRTP、UDP 和接收端；
- WebRTC 的 TWCC/GoogCC 必须通过真实媒体反馈产生 `SetRates`，再驱动语义准入；
- 应用只观测和消费 WebRTC 给出的容量，不用定时器伪造目标码率。

Task 11 的产物是体积视频传输研究平台的控制与测量闭环，不是播放器。

## 2. 范围与非目标

### 2.1 本任务包含

1. 让 OCTREE-SIM 成为可实际收发的模拟媒体 codec。
2. 为 sendonly PeerConnection 接入一个最小调度源，持续触发 encoder。
3. 协商 transport-wide congestion control RTP 扩展和 `transport-cc` RTCP feedback。
4. 将 encoder `SetRates` 作为高频容量控制入口。
5. 将公开 RTCStats 映射为低频、只读的传输观测快照。
6. 在应用不可直接取得 pacer delay 时，以最小 WebRTC 只读补丁公开已有的 `Call::Stats::pacer_delay_ms`。
7. 用单元、契约、真实 localhost 集成和容量阶跃测试证明链路。

### 2.2 本任务不包含

- 真实八叉树、G-PCC、V3C 或任何点云 codec；
- 真实相机、点云文件、GPU、窗口或渲染器；
- Task 12 的原生 ULPFEC repair packet 生成；
- Task 13 的 post-FEC 恢复与语义块组装；
- 用 DataChannel 或旁路 UDP 发送媒体；
- 将单个语义 block 伪装成一张独立视频帧；
- 通过私有定时器直接修改应用码率来冒充 GoogCC。

Task 12/13 只有在 Task 11 的真实 RTP 与反馈闭环验收通过后才开始。

## 3. 固定的数据模型

每个模拟体积视频源帧的逻辑大小沿用现有工程契约：十进制 2 MB（2,000,000 字节），由固定 seed 和 `frame_id` 确定性生成。测试必须能够对同一 seed/frame_id 复现 byte-exact 数据。

语义比例固定为：

| 优先级 | 占比 | 每个 2 MB 帧的逻辑字节 | Task 11 行为 |
| --- | ---: | ---: | --- |
| P0 | 1/8 | 250,000 | 最后才丢弃 |
| P1 | 3/8 | 750,000 | P0 之后准入 |
| P2 | 1/2 | 1,000,000 | 带宽下降时最先丢弃 |

2 MB 是生成端的逻辑输入规模，不代表每帧都必须发送 2 MB。准入控制根据 WebRTC 的目标码率、目标帧率和协议开销预算选择当帧实际进入 OCTREE-SIM access unit 的数据。未准入的 P1/P2 记录为应用层 admission drop，不进入 encoder 输出，也不进入 pacer。

P0/P1 的 FEC 比例仍由既有 protection policy 描述，但 Task 11 的发送字节只计 source payload。实际 repair packet 与其真实开销从 Task 12 开始计入。

## 4. 架构边界

依赖方向保持为：

```text
simulation/model/protection/transport policy
                    ^
                    |
        rtc adapter + metrics adapter
                    ^
                    |
             native WebRTC
```

### 4.1 内层策略

`transport_feedback` 定义与 WebRTC 类型无关的接口和值对象：

- `EncoderRateUpdate`：目标码率、目标帧率和采样时间；
- `RtcTransportObservation`：低频 RTCStats 的可选观测；
- `TransportFeedbackSnapshot`：合并后的不可变快照；
- `TransportFeedbackStore`：线程安全地接收高频 rate update 和低频 stats update；
- `ActualBitrateSampler`：以单调时间和 outbound byte counter 差分计算实际发送码率。

所有可能缺失的 RTCStats 字段使用 `std::optional`。缺失值不能被写成 0，因为 0 RTT、0 loss 和 0 queue delay 都是有效但含义不同的实测值。

### 4.2 WebRTC 适配层

`OctreeSimEncoder` 只承担三件事：

1. 接收 WebRTC 调度用的微型 I420 frame；
2. 从有界 simulated access-unit queue 取出同一 RTP timestamp 对应的已准入字节；
3. 构造 `EncodedImage` 并调用 `OnEncodedImage`。

它不压缩、不改变语义字节、不自行分配带宽。没有 callback、未初始化或找不到匹配 access unit 时返回明确 codec 状态并计数，不静默伪造成功。

`OctreeSimDecoder` 校验 access-unit 头、长度、frame_id 和 checksum，记录“接收/模拟解码完成”。为满足 WebRTC 视频接收管线的生命周期要求，它可回调一张固定尺寸的微型 I420 completion frame；该 frame 只是完成信号，内容不被展示，也不代表点云重建结果。

`SimulatedFrameTriggerSource` 每个逻辑源帧只产生一次调度信号。它先把已准入 access unit 写入 queue，再向 WebRTC video source 推送相同 timestamp 的微型 frame，避免 encoder 先于数据入队。

### 4.3 会话层

`PeerConnectionSession` 负责：

- sendonly 端创建并挂接 OCTREE-SIM video track；
- recvonly 端接收该 track，但不创建 renderer；
- 异步采集现代 `RTCStatsReport`，交给独立 mapper；
- 保证 stats 回调、track/source 和 codec factory 的生命周期早于 WebRTC 线程结束；
- 保持 Task 10 已验证的 ICE/DTLS 生命周期语义。

策略层不得 include WebRTC header。RTCStats 的遍历、单位转换和 selected candidate pair 关联全部留在 `rtc` adapter。

## 5. 数据与控制流

### 5.1 高频控制路径

```text
TWCC feedback
  -> GoogCC estimate
  -> WebRTC encoder SetRates(target_bitrate, fps)
  -> TransportFeedbackStore
  -> TransportBudget
  -> SemanticAdmissionController
  -> admitted P0/P1/P2 bytes for the next source frame
```

控制路径的唯一权威容量输入是 `SetRates`。RTCStats 中的 `availableOutgoingBitrate` 只用于观测和交叉校验，不能在另一条路径上覆盖高频目标，从而避免双控制器振荡。

### 5.2 媒体路径

```text
deterministic 2,000,000-byte simulated frame
  -> semantic partition P0/P1/P2
  -> admission using latest WebRTC rate budget
  -> one OCTREE-SIM access unit
  -> EncodedImage callback
  -> WebRTC packetizer/pacer/SRTP/UDP
  -> receiver depacketizer
  -> OCTREE-SIM decoder validation/completion event
```

一个逻辑体积帧对应一个 OCTREE-SIM encoded access unit。RTP 分片由 WebRTC 根据 MTU 完成。Task 11 不把 region/block 边界映射为独立视频帧，防止破坏帧率、GoogCC 编码预算和后续 FEC 语义。

### 5.3 低频观测路径

RTCStats 默认每 500 ms 采样一次，测试可以注入不同周期。快照包含：

- `target_bitrate_bps`、`target_framerate_fps`；
- selected candidate-pair `currentRoundTripTime`；
- selected candidate-pair `availableOutgoingBitrate`；
- outbound RTP bytes/packets；
- outbound retransmitted bytes/packets；
- outbound total packet send delay；
- remote-inbound fraction loss、jitter 和 round-trip time；
- pacer delay；
- 由连续 outbound byte counter 和单调时间计算的 actual sent bitrate。

所有单位在 mapper 边界统一为 `bps`、`milliseconds` 或无量纲比例。counter 回退、timestamp 不前进、SSRC/report 暂未出现时，sampler 返回 unavailable 并等待下个样本，不生成负码率或无穷值。

## 6. Pacer 观测补丁边界

当前公开 RTCStats 没有可靠的 pacer queue delay。WebRTC 内部 `Call::Stats` 已经从 `transport_send_->GetPacerQueuingDelayMs()` 取得 `pacer_delay_ms`。Task 11 允许一个只读、无算法行为变化的 vendored patch：

1. 在现代 candidate-pair stats 对象上增加非标准可选字段 `pacerDelay`，单位秒，与 W3C stats 的时间单位保持一致；
2. `RTCStatsCollector` 仅对当前 selected/best candidate pair 填入 `Call::Stats::pacer_delay_ms / 1000.0`；
3. 不修改 pacer、GoogCC、队列调度、发送速率或丢包处理算法；
4. 增加上游窄单元测试并更新 `webrtc_patches/manifest.json` 与说明。

Task 11 不公开无法从当前 `Call` 接口可靠获得的 pacer queue bytes。该字段保持 unavailable，禁止从 target bitrate 和 delay 猜成“实测 queue bytes”。

## 7. 线程、安全与背压

- `SetRates`、RTCStats callback、模拟帧生成与 encoder callback 可能处于不同线程；共享反馈状态和 access-unit queue 必须线程安全。
- queue 有固定小容量，默认最多 2 个尚未编码的 access unit，以“最新帧优先”淘汰最旧未编码帧；淘汰必须产生可观测计数。
- Stop 顺序为：停止生成新帧，取消 stats 轮询，解绑 source/track，关闭 PeerConnection，释放 factory，最后停止 signaling/worker/network threads。
- callback 不持有裸的 session 所有权；异步回调通过共享状态和 stopped 标志拒绝晚到事件。
- checksum 或长度不匹配属于可测协议错误：该 access unit 不计为 decode complete，同时记录 receiver validation failure。

## 8. 指标语义

每个采样事件至少带 `run_id`、单调时间和可选 wall-clock 时间。Task 11 输出或暴露以下研究指标：

- GoogCC target bitrate；
- admitted source payload bytes，按 P0/P1/P2 分列；
- admission drop bytes，按 P0/P1/P2 分列；
- estimated RTP/SRTP/UDP overhead（必须标记 `estimated`）；
- pacer delay（WebRTC 内部观测，标记 `observed`）；
- actual sent bitrate（RTCStats counter 差分，标记 `derived`）；
- selected pair RTT、loss、jitter、retransmission counters；
- encoded callback count、received access-unit count、decode completion count；
- source queue eviction 和 receiver validation failure count。

指标名称必须区分 `target`、`observed`、`derived` 和 `estimated`，避免把推算值包装为底层实测值。

## 9. 测试策略

### 9.1 纯单元测试

- 反馈 store 合并高频和低频更新，缺失字段保持 unavailable；
- actual bitrate sampler 的首样本、正常差分、零时间差、counter 回退；
- 2,000,000 字节帧的 P0/P1/P2 byte-exact 比例；
- bitrate 降低时准入顺序为先 P2、再 P1、最后 P0；
- access-unit queue 的 timestamp 匹配、有界容量和最新帧优先淘汰；
- simulated access-unit 头、长度、checksum 的成功和损坏路径；
- RTCStats mapper 的 selected pair 关联、单位换算、缺失字段和多 SSRC 汇总。

### 9.2 WebRTC 契约测试

- offer/answer SDP 同时包含 transport-wide extension 与 OCTREE-SIM 的 `transport-cc` feedback；
- encoder factory 创建的真实 encoder 收到 WebRTC `SetRates`，并把更新送入共享 feedback store；
- encoder 的非空 `EncodedImage` callback 与 decoder 的校验完成事件可以在无 UI 条件下发生；
- 上游 stats collector 测试证明 pacer delay 只写到 selected pair，且毫秒到秒转换正确。

### 9.3 真实 localhost 集成测试

两个 PeerConnection 使用真实 host/UDP ICE、DTLS/SRTP：

1. 至少发送 30 个确定性 simulated access unit；
2. sender outbound bytes/packets 必须增长；
3. receiver inbound/decoded completion 必须增长；
4. `SetRates` 至少发生一次且值大于 0；
5. transport feedback 相关协商存在；
6. pacer delay 字段可由 selected candidate pair 读出；
7. 不启动窗口、不依赖摄像头或 GPU。

测试失败时保留足够的 SDP、状态转换和关键 counter，但不永久保存 SRTP 密钥或完整抓包。

### 9.4 容量阶跃测试

容量序列固定为 8 Mbps → 3 Mbps → 0.8 Mbps → 4 Mbps。测试重点是方向与控制闭环，不要求每个平台得到完全相同的瞬时数值：

- WebRTC BWE/encoder target 在下降阶段总体下降，在恢复阶段总体回升；
- admission 随 target 降低依次减少 P2、P1，P0 最后受影响；
- actual sent bitrate 在稳定窗口内不长期高于 target 加允许的协议与采样误差；
- pacer delay 在拥塞阶段可升高，但解除限速后应回落，不能单调无界增长。

完整的容量阶跃优先使用 WebRTC 自带 fake network/call harness，以获得确定性；真实 localhost 测试证明接线和加密传输。两层证据必须同时存在，不能只用纯应用单元测试声称 GoogCC 闭环成立。

## 10. 验收标准

Task 11 只有同时满足以下条件才可标记完成：

1. OCTREE-SIM 在无 UI 条件下产生真实 RTP 流量，sender/receiver counters 增长；
2. SDP 明确协商 TWCC extension 和 `transport-cc` feedback；
3. `SetRates` 来自 WebRTC codec 回调并进入 feedback store；
4. 语义准入实际消费最新 target bitrate，容量下降时优先淘汰 P2；
5. RTCStats 快照能够区分缺失值和真实 0；
6. 日志或测试可同时看到 target、admitted payload、estimated overhead、pacer delay 和 actual sent bitrate；
7. pacer delay 补丁只有只读观测变化，具备上游测试和补丁清单；
8. 新增测试全部先出现预期失败，再以最小实现转绿；
9. 既有 Task 7–10 全套测试继续通过，真实 ICE/DTLS 测试不得 skip；
10. WebRTC 工作树中既有用户改动完整保留，没有 reset、clean、sync 或大范围格式化。

## 11. Task 12/13 的衔接约束

Task 12 在本设计的 access-unit 与真实 RTP 基础上增加 P0/P1 source/repair 保护，并保证 repair 进入同一个 WebRTC egress、pacer 和 SRTP；不得另建媒体 socket。Task 13 在 receiver 的 post-FEC source-payload observer 处解析 wire header、标记 recovered source，并送入独立 block assembler。

Task 11 的反馈 store、指标命名、模拟数据复现和无 UI 接收完成事件均作为后续任务的稳定接口。后续任务可以扩展 source/repair 账本，但不得绕过或复制一套新的拥塞控制器。
