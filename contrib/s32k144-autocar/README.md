# contrib · s32k144-autocar（Simulink_AutoCar 可移植资产）

本目录是**另一个项目**（`Simulink_AutoCar`，S32K144 电机控制 / ADAS 的 MBD 骨架）归档时
保留下来的一小部分可复用资产。**它不是 VVOS 的一部分功能模块**，不参与 VVOS 的运行时，
只作为技术档案与仿真资产保留。

---

## 1. 出处与项目定位（如实说明）

| 项 | 值 |
|---|---|
| 源仓库 | `https://github.com/Nagi-226/Simulink_AutoCar` |
| 分支 | `main` |
| commit | `bedb358a0ac60f8111f5ae0d691d1cbf64b0cdb2`（`v0.5.1: 全项目集成修复 + FOC安全缺陷修复`） |
| 源仓 `VERSION` | `0.5.1`（已原样拷入本目录 `VERSION`） |
| 许可证 | **无**。原项目未附任何许可证文件（已核实：源仓根目录无 `LICENSE` / `COPYING`）。**此处仅作个人技术档案留存，不构成任何授权**；对外再分发前必须自行确认权利归属 |

### 关于"这是一个 MBD 工程"的说法——不成立，这里写清

源项目标称 Simulink 基于模型设计（MBD）骨架，但**实测**：

- `02_Models/` 下 `.slx` 模型文件数 = **0**（仅有 `algorithms/ application/ diagnostics/ drivers/ integration/` 五个目录）
- `04_CodeGen/` 下文件数 = **0**（没有任何生成代码）

也就是说：**没有模型、没有生成代码、没有可重跑的 MBD 链路**。它是一副目录与接口骨架，
其中少数手写 C 文件是真资产（就是下面三类）。**不要把它当成可复现的 MBD 工程来引用。**

---

## 2. 三类资产各自能干什么

### 2.1 `dbc/` —— CAN 信号契约（**纯数据，不参与编译**）

| 文件 | 报文数 | 信号数 | 用途 |
|---|---:|---:|---|
| `simulink_autocar.dbc` | 5 | 24 | 早期整车信号集（v01） |
| `simulink_autocar_v02.dbc` | 8 | 45 | 扩展后的信号集（v02，含更多报文） |
| `radar_ultrasonic_v03.dbc` | 7 | 49 | 雷达 + 超声波传感器信号集（v03） |

（报文/信号数由 `grep -cE '^BO_ '` / `'^ SG_ '` 实测，不是估算。）

**能干什么**：可直接在 **CANoe / CANalyzer** 里加载（`File → Open → Database`），
用来做信号级仿真、报文编辑、面板搭建与回放；也是写 DBC 解析器时现成的测试输入。

**不能干什么**：VSCode / CMake / VVOS 构建链**不读它**。见 §3。

### 2.2 `capl/` —— CANoe 仿真与故障注入脚本（**不参与编译**）

| 文件 | 行数 | 干什么 |
|---|---:|---|
| `radar_simulator.can` | 259 | 模拟雷达目标（距离/速度/角度）周期性发报文，供 ADAS 逻辑联调 |
| `ultrasonic_simulator.can` | 194 | 模拟超声波测距报文 |
| `sensor_error_injection.can` | 153 | 注入传感器故障（超时、越界、丢帧），验证下游容错 |
| `canfd_test.capl` | 216 | CAN-FD 帧收发与一致性测试脚本 |

**能干什么**：在 CANoe 里挂到对应 DBC 上即可跑，不需要源项目的任何代码；
`sensor_error_injection.can` 的思路（构造越界/超时帧）可以照搬到任何 CAN 台架。

**前提**：需要 **Vector CANoe + 对应 license**。没有 CANoe 时这些脚本无法执行，
只能作为写法参考。

### 2.3 `uds/` —— UDS 诊断传输层（**这是唯一参与编译的资产**）

`uds_transport.c` / `uds_transport.h`（419 / 185 行）

- 实现 **ISO 14229-1**（UDS）中的 `0x10` DiagnosticSessionControl、`0x11` ECUReset、
  `0x22` ReadDataByIdentifier、`0x27` SecurityAccess、`0x3E` TesterPresent；
  以及 **ISO 15765-2**（CAN TP）分帧。
- **零平台依赖**（实测 include 只有 `<stdint.h>` `<stdbool.h>` + 自身头文件），
  因此可移植到任意 C99 环境，与 S32K144 / ARM 无关。
- 头注释声明按 **MISRA C:2012（ASIL B 强制规则）** 编写。
- API 只有四个：`UDS_Init` / `UDS_ProcessRequest` / `UDS_CheckTimeout` / `UDS_DeInit`，
  与硬件解耦（收发帧由调用方负责）。

### ⚠️ 已知语义偏差（引用前必读）

`UDS_HandleTesterPresent()` 把 **`subFunc == 0x00` 当作"抑制响应"**，`0x01..0x7F` 才回正响应
（`uds_transport.c` 内注释原文：*"Sub-function 0x00 = no response required"*）。

这与 ISO 14229-1 不一致：标准里 **bit7（`0x80`）** 才是 suppressPosRspMsgIndicationBit，
`3E 00` 应当**回** `7E 00`。**本目录保留原实现未改**（属上游资产，改动会失去"原样留存"的意义）；
接入方若需要标准行为，请在调用侧把 `3E 80` 映射成 `subFunc = 0x00`，或自行修正后另行标注。
本目录的冒烟测试用的是 `subFunc = 0x01`，正是为了绕开这处偏差。

---

## 3. 明确声明：dbc / capl **不参与编译**

- 构建系统只编译 `uds/uds_transport.c`（目标 `vr_contrib_uds`，见仓库根 `CMakeLists.txt`）。
- `dbc/`、`capl/` 是**数据与脚本**：CMake 不解析、编译器不接触、ctest 不执行。
  把整个 `dbc/`、`capl/` 删掉，VVOS 的构建与测试**不受任何影响**。
- 因此它们也**不会**出现在任何构建产物、安装清单或运行时依赖里。

---

## 4. 目录结构

```
contrib/s32k144-autocar/
├── README.md                      本文件
├── VERSION                        源仓 VERSION 原样拷贝（0.5.1）
├── dbc/                           3 个 CAN 数据库（不参与编译）
│   ├── simulink_autocar.dbc
│   ├── simulink_autocar_v02.dbc
│   └── radar_ultrasonic_v03.dbc
├── capl/                          4 个 CANoe 脚本（不参与编译）
│   ├── canfd_test.capl
│   ├── radar_simulator.can
│   ├── sensor_error_injection.can
│   └── ultrasonic_simulator.can
├── uds/                           唯一参与编译的资产
│   ├── uds_transport.c
│   └── uds_transport.h
└── test/
    └── uds_smoke_test.cpp         编译后可用的最小冒烟验证（0x3E → 0x7E）
```

## 5. 为什么没有并入 S32K144 的 HAL / Bootloader / 跟踪算法

同源项目里还有 `03_BSP/s32k144_sdk/`（裸机 HAL，含 FlexCAN 驱动）、
`06_OtaBootloader/bootloader_src/`（仅 2 个骨架文件）、
`02_Models/algorithms/adas_tracking/kalman_tracker.c`（ADAS 跟踪算法）。
三者**均未收录**：

- HAL 属**另一条 MCU 产品线**的驱动层，与本机工具链（MinGW / 无 `arm-none-eabi`）不匹配，
  收进来就是**不可编译的死代码**，且会让人误以为 VVOS 支持 S32K144 目标；
- Bootloader 仅两个文件、无构建、无入口，是骨架不是资产；
- `kalman_tracker.c` 在 VVOS 的职责边界内**没有归属**（VVOS 不做感知/跟踪）。

同理，`02_Models/` 下其余空壳目录、`04_CodeGen/`（0 文件）、`01_Docs/` 的全部文档均未收录。
