![Virtual-Vehicle-OS 架构设计图示](https://github.com/user-attachments/assets/9595361e-9a97-470d-be79-5c125619ca62)

# Virtual-Vehicle-OS

**车载域 ↔ 机器人域的消息互通中间件**（C++17，零第三方依赖）。

它只做一件事：**定义并实现"一条消息长什么样"** —— 帧格式、信封字段、编解码、路由、投递语义（TTL / 背压 / 幂等）。业务逻辑、感知算法、硬件驱动都不在这里。

---

## 它现在真的在服役

VVOS 已经不是"待验证的中间件"，**它是 RoboMind 感知链路的协议承载层**：

```
RoboMind-S3（ESP32-S3 板子）
    │  ppm_run 产出感知结论
    │  wire_encode()  ← 按本仓库定义的格式编码（11 字段，独立实现）
    ▼
[ TCP：4 字节大端长度 + 11 字段文本 ]
    │
    ▼
RoboMind-Console（PC 端，Qt6）
    │  MessageCodec::Decode()  ← 直接链接本仓库
    ▼
    显示"有人/无人 · 疲劳等级"
```

| 事实 | 意义 |
|---|---|
| **wire 格式的真相源在本仓库** | `src/interconnect/message_codec.cpp` 是权威实现；MCU 侧与 PC 侧都对齐它 |
| MCU 侧是**独立实现**、不是链接 | 跨端一致性靠**测试向量**保证，不靠编译器 |
| PC 侧**直接 `add_subdirectory` 链接** | 同进程零 IPC，契约层完全一致 |

> 完整拓扑、依赖形式与"改一处要同步哪里"，见 **[项目集群总索引](https://github.com/Nagi-226/RoboMind-S3/blob/master/docs/PROJECTS.md)**。

---

## 怎么用（可嵌入，最小示例）

本仓库是**可被父工程嵌入的静态库**（2026-11 修复了两层嵌入问题，见下方"当前状态"）。

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

# 1) 引入（不要求装到系统里，直接指目录即可）
set(VVOS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../Virtual-Vehicle-OS")
set(ENABLE_TESTS     OFF CACHE BOOL "" FORCE)   # 不构建 VVOS 自带测试
set(ENABLE_SANITIZER OFF CACHE BOOL "" FORCE)
add_subdirectory("${VVOS_ROOT}" vvos_build EXCLUDE_FROM_ALL)

# 2) 链接：头文件路径由 vr_base_framework 的 PUBLIC include 自动带出
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE vr_base_framework)
```

```cpp
#include "interconnect/message_codec.hpp"

vr::interconnect::MessageEnvelope env;
if (vr::interconnect::MessageCodec::Decode(text, &env) == vr::core::ErrorCode::kOk) {
    // env.topic / env.sequence / env.ttl_ms / env.payload / env.source ...
}
```

**导出的库目标**：`vr_base_framework`（主库）、`commonsvc_log`、`vr_error_code`（后两者由主库 `PUBLIC` 带出）。

---

## 当前状态（实测，2026-11-13）

| 项 | 状态 | 怎么验 |
|---|---|---|
| 编译 | **成功**，55 个构建目标全过 | `cmake --build build` |
| 编译告警 | **0** | `-Werror` 下"0 error"即 0 warning（`ENABLE_STRICT_WARNINGS` 默认 ON：`-Wall -Wextra -Wpedantic -Werror`） |
| 测试 | **17 / 19 通过** | `ctest --test-dir build` |
| 可嵌入性 | **`add_subdirectory` 可用** | 父工程消费头文件已验证，证据见 `docs/smoke/` |
| 零第三方依赖 | 是（仅系统库 `ws2_32` / `pthread` / `rt`） | — |

### 两个失败测试：都是**测试自身缺陷**，不是产品缺陷

| 测试 | 失败断言 | 归因 | 记录 |
|---|---|---|---|
| `interconnect_fault_injection_test` | `handler error should be counted` | fake transport **从不回投**，入站断言**不可达** | `docs/backlog.md` BL-03 / BL-04 |
| `f429_bridge_diag_adapter_test` | `snapshot file should be created` | 测试**依赖 cwd 下的 `build/` 目录** | `docs/backlog.md` BL-05 |

> 这两条是"测试写错/写不稳"，**产品路径本身没有被证明有问题**；但反过来也要说清：**它们本应覆盖的那两条路径，当前没有测试保护**。

### 可嵌入性修复的两层

| 层 | 症状 | 修复 |
|---|---|---|
| 一 | VVOS 自己在父工程内构建时，**找不到自己的头** | `c361c00`：`CMAKE_SOURCE_DIR` → `CMAKE_CURRENT_SOURCE_DIR` |
| 二 | 父工程**消费** VVOS 时找不到 VVOS 的头 | `13c46b0` / `a6cea7e`：`vr_base_framework` 导出 `PUBLIC` include |

证据：`docs/smoke/embed-evidence.md`、`docs/smoke/embed-export-evidence.md`。

---

## 目录结构

```
src/core · src/log · src/ipc（仅非 Windows） · src/interconnect（核心：编解码/路由/Bridge/策略/传输）
platform/stm32f429（默认下架） · tests/ · tools/（发布门禁） · docs/（评估 · Backlog · 规格 · 历史）
```

---

## 文档地图

| 想了解 | 看 |
|---|---|
| **改了 wire 格式要同步哪里**（必读） | [项目集群总索引](https://github.com/Nagi-226/RoboMind-S3/blob/master/docs/PROJECTS.md) §4 |
| 已知缺陷与待决策项 | [`docs/backlog.md`](./docs/backlog.md) |
| 架构体检（严重度清单 A1–A16） | [`docs/architecture-assessment-2026-11-13.md`](./docs/architecture-assessment-2026-11-13.md) |
| 三者打通评估（VVOS × RoboMind × PPM） | [`docs/three-way-integration-2026-11-13.md`](./docs/three-way-integration-2026-11-13.md) |
| wire 格式的字节级规格 | [`docs/u02-u03-implementation-spec-2026-11-13.md`](./docs/u02-u03-implementation-spec-2026-11-13.md) |
| 版本历史、验收清单、审计报告 | [`docs/version-history.md`](./docs/version-history.md) |
| README 重排规划（历史记录） | [`docs/readme-restructure-plan.md`](./docs/readme-restructure-plan.md) |

---

## 构建与测试

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

发布门禁（6 类检查；失败会按脚本内的 `Resolve-Owner` / `Resolve-Action` **分派到责任域与处置动作**）：

```
tools/release_gate_v09.ps1 -BuildDir build
```

检查范围：required tests、benchmark、soak、fault assertion、stm32 产物、f429 诊断快照。

---

## 已知限制（都是**有意的**，不是待办）

| 限制 | 原因 |
|---|---|
| `ENABLE_STM32F429_TARGET` 默认 **OFF**，**打开会直接 `FATAL_ERROR`** | 该目标缺 2 个 TU、且 C++ 运行库配置未定型；**显式下架**好过产出一个链接失败的半成品 |
| `demo/` 与 `ipc/posix_message_queue` **在 Windows 上不构建** | 平台依赖；Windows 侧靠 `tests/interconnect/*` 覆盖 |
| **没有 TCP Server 能力**（`TcpTransport` 是客户端） | 当前所有场景都是"设备连出去"；需要"VVOS 当服务端"时见 `docs/backlog.md` |
| 指标读取有 **≤100 ms 滞后** | `GetBridgeMetrics()` 与 `Export*` 读缓存快照；要强一致用 `CaptureMetricsSnapshot()` |
*许可证：按仓库现有 LICENSE 文件；若不存在，以项目负责人说明为准。*
