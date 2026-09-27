# Scaffold Manifest

## 当前状态

本文档记录 2026-09-27 创建的“注释骨架”。所有 `.h`、`.cc`、`.ps1` 和 `CMakeLists.txt` 文件当前只描述职责与测试入口，不提供功能实现，不能据此声称工程可构建或可运行。

## 层次

1. `base/config`：错误、配置、单位和运行清单。
2. `model/simulation`：模拟点云帧、语义区域和确定性生成。
3. `transport/protection`：分块、线格式、预算、准入和 FEC 策略。
4. `rtc/signaling`：WebRTC 适配、控制面和信令。
5. `receiver`：块恢复、残缺帧和 deadline。
6. `latency/metrics`：时钟、事件、JSONL/CSV 和实验结果。
7. `src/apps`：三进程装配层，不放业务算法。
8. `test/test_volumetric_*`：与上述层次一一对应的测试工程。

## 骨架完成标准

- 每个计划模块都有明确文件归属。
- 每个接口文件写明职责、输入、输出和不变量。
- 每个实现文件写明未来实现顺序和禁止事项。
- 每个测试文件写明应先失败的行为清单。
- 未修改 `extern/webrtc/src`，未修改现有 `test/CMakeLists.txt`。
