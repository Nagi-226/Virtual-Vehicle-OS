# 证据：VVOS 可嵌入性修复（`fix/cmake-embeddable`）

- 分支：`fix/cmake-embeddable`（基于 `master@41431ff`）
- 日期：2026-11-13
- 改动集：**仅** `CMakeLists.txt`（3 处 `CMAKE_SOURCE_DIR` → `CMAKE_CURRENT_SOURCE_DIR`），
  外加本证据文件。**未改任何 `src/` 代码。**
- B 组口径说明：`docs/BRANCHING.md` 存在于 `RoboMind-S3/docs/`，**VVOS 仓库内没有该文件**
  （`ls docs/BRANCHING.md` ⇒ 不存在）。本文按其 A–E 组结构组织，但判据换成 VVOS 自己的口径
  （构建 exit 0 + `ctest` 17/19 + 父工程嵌入验证），并逐条标明 **N/A** 的原因。

> 认知标记：**VERIFIED** = 附命令输出/文件行号；**INFERRED** = 给依据但未实测；**UNKNOWN** = 说明缺哪一环。
> 本次全部判据均为 **VERIFIED**，唯一的 INFERRED 已单独标注。

---

## 0. 问题与根因

`CMakeLists.txt:29`（修前）：

```cmake
include_directories(${CMAKE_SOURCE_DIR}/src)
```

`CMAKE_SOURCE_DIR` = **当前工程顶层源码目录**。VVOS 被父工程 `add_subdirectory()` 引入时，
它指向**父工程根**，于是编译期 `-I` 变成 `<父工程>/src`，VVOS 自己的头文件全部找不到。

**VERIFIED（根因）**——修前父工程构建的编译命令里：

```
-ID:/Temp/embed-test/src        ← 父工程根，不是 VVOS 根
```

---

## A 组 · 构建

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| A1 | 构建通过（exit 0） | ✅ | 独立构建：`configure=0 build=0 error=0 warning=0`；嵌入构建：`BUILD_EXIT=0` |
| A2 | 烧的就是编的 | **N/A** | VVOS 是 PC 侧库，无烧录环节 |
| A3 | 烧录无跳过 | **N/A** | 同上 |
| A4 | 启动无崩溃 | **N/A** | 同上（进程级启动由使用方负责） |

**顺带说明"改前也能编"的排除**：独立构建在改前就是通过的（否则仓库历史不可能 15 个 commit 全绿），
所以**只证明"改后能编"无法说明任何事**。本次采用了 C1 的"同源目录 A/B 对比"来隔离变量。

---

## B 组 · 默认态零影响（本单无特性开关，改用"同源目录 A/B"证明）

本单**未新增任何 `CONFIG_*`/`option()`**，B 组 B1–B4 的原始形态（查宏、查串口）
不适用。等价证明：**同一源目录、只换 CMakeLists，比较生成的编译命令**。

**VERIFIED**——用 `git worktree add` 检出 `master` 到 `/tmp/vvos-master`（同一路径不变），
先编 master 版 CMakeLists，再覆盖为修复版重编：

| 目标 | 改前 `-I` | 改后 `-I` | 一致 |
|---|---|---|---|
| `src/core/error_code.cpp`（:29 的影响面） | `-ID:/Temp/vvos-master/src` | `-ID:/Temp/vvos-master/src` | ✅ 逐字相同 |
| `f429_bridge_diag_adapter_test.cpp`（:247 的影响面） | `-ID:/Temp/vvos-master/src` `-ID:/Temp/vvos-master` | 同左 | ✅ 逐字相同 |

⇒ **独立构建时 `CMAKE_SOURCE_DIR == CMAKE_CURRENT_SOURCE_DIR`，行为零变化。**

> **INFERRED（唯一一条）**：`:88` 的 `-T<ld>` 只在 `ENABLE_STM32F429_TARGET=ON` 时生效，
> 该选项默认 `OFF`，因此本机未实测其路径变化。依据：两变量在顶层构建时相等（与上表同理），
> 且链接脚本路径属于本仓库文件（`platform/stm32f429/STM32F429ZGTX_FLASH.ld` 存在）。

---

## C 组 · 关键行为验证

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| C1 | 声明的**主行为**成立：父工程 `add_subdirectory` 后 VVOS 能编 | ✅ | 见下方"改前/改后"对照 |
| C2 | 边界/降级路径跑过 | ✅ | ① 父工程 `set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)` 生效：嵌入构建 `ctest -N` ⇒ **Total Tests: 0**，`vvos_build/` 下 **0 个** 测试可执行文件，`CMakeCache.txt` 里 `ENABLE_TESTS:BOOL=OFF`；② 父工程**不补** `-I` 时的失败也如实记录（见 C5 说明） |
| C3 | 没有"零吞吐式通过" | ✅ | 不只看"编过"：父工程产物**实际运行**，输出 `encode_ec=0 len=48` / `decode_ec=0 topic=t`（真跑通编解码，不是只链接成功） |
| C4 | 计数器与采集窗口一致 | **N/A** | 本单不涉及任何计数器/统计窗口 |

### C1 的改前 / 改后对照（核心证据）

父工程 `D:/Temp/embed-test`（**不入库**）：

```cmake
cmake_minimum_required(VERSION 3.16)
project(embed_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("E:/Github Project/Virtual-Vehicle-OS" vvos_build)
```

| 阶段 | 命令 | 结果 |
|---|---|---|
| **改前** | `cmake --build build-before` | **`BUILD_EXIT=1`，13 个 error**，全部形如 `fatal error: log/logger.hpp: No such file or directory` |
| **改后** | `cmake --build build-after` | **`BUILD_EXIT=0`，0 error**，产出 `libvr_base_framework.a` / `libcommonsvc_log.a` / `libvr_error_code.a` |

改前错误首行（逐字）：

```
E:/Github Project/Virtual-Vehicle-OS/src/log/logger.cpp:1:10: fatal error: log/logger.hpp: No such file or directory
```

### C5（重要）父工程**消费头文件**仍需额外一步 —— 本单**未做**，已报主理人

`grep -n target_include_directories CMakeLists.txt` ⇒ 只有两条 **PRIVATE**（`:91`、`:256`），
**`vr_base_framework` 没有导出 PUBLIC include**；头文件路径完全依赖目录级的
`include_directories()`，而它**不向父工程传播**。

**VERIFIED**（父工程 `target_link_libraries(embed_test PRIVATE vr_base_framework)` 且**不补** `-I`）：

```
PARENT_ADDS_INCLUDE=OFF : build=1  errors=1
  D:/Temp/embed-test/main.cpp:4:10: fatal error: interconnect/message_codec.hpp: No such file or directory

PARENT_ADDS_INCLUDE=ON  : build=0  errors=0
  encode_ec=0 len=48
  decode_ec=0 topic=t
```

⇒ 本单修好的是"**VVOS 自己在父工程内构建**"；要让父工程**直接 include VVOS 头**，
还需 `target_include_directories(vr_base_framework PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)`
（CI 里再加一个 `install(EXPORT)` 更完整）。**该改动未实施**——属新增对外构建接口，
超出"1 行修复"授权范围，等待主理人决定。

> **UNKNOWN**：Qt 宿主工程最终采用"补 `-I`"还是"由 VVOS 导出 PUBLIC include"尚未确定；
> 两条路都能工作（上表已分别验证），但影响后续 CMake 接口形态。

---

## D 组 · 静态资源与内存

VVOS 是 PC 侧库，没有 MCU 的 `.dram0.bss` 口径。等价判据：**产物清单不变、无新增依赖**。

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| D1 | 必需产物还在 | ✅ | 独立构建 3 个静态库：`libvr_error_code.a` / `libcommonsvc_log.a` / `libvr_base_framework.a` |
| D2 | 没有引入新的静态存储 / 新依赖 | ✅ | 改动只在 CMake 路径表达式上；`git diff --stat CMakeLists.txt` ⇒ `1 file changed, 12 insertions(+), 3 deletions(-)`，**无 `target_link_libraries` 变更、无新增源文件** |
| D3 | `.text` 变化 | **N/A** | 无代码改动 |
| D4 | 新运行期分配 | **N/A** | 同上 |

---

## E 组 · 文档

| # | 判据 | 结果 | 说明 |
|---|---|---|---|
| E1 | 改了行为则文档同步 | ✅ | `CMakeLists.txt` 三处改动均带行内注释，写明"为什么必须 `CMAKE_CURRENT_SOURCE_DIR`"以及**踩过什么**（13 个 TU 的 `fatal error`） |
| E2 | 勘误旧结论 | **N/A** | 本次不涉及对既有结论的更正 |
| E3 | 未解决的边界写明 | ✅ | 见下方 Non-Goals + C5（PUBLIC include 未导出） |

---

## 测试与门禁

```
独立构建：configure=0  build=0  error=0  warning=0
ctest   ：89% tests passed, 2 tests failed out of 19        ← 17/19，与改动前一致
失败项  ：interconnect_fault_injection_test（BL-03）/ f429_bridge_diag_adapter_test（BL-05）
          —— 两条都是已登记的测试自身缺陷，不是产品失效

门禁    ：declared=19 registered=19 executed=19
          block_reasons = ctest_suite_failed
                        + required_tests_failed:interconnect_fault_injection_test
                        + test_failed:f429_bridge_diag_adapter_test
          （与改动前逐项相同，无新增红因）
```

## 改动文件

```
CMakeLists.txt                 3 处 CMAKE_SOURCE_DIR → CMAKE_CURRENT_SOURCE_DIR（+ 行内注释）
docs/smoke/embed-evidence.md   本文件
```

## Non-Goals（本次明确不做）

1. **不改 `ENABLE_TESTS` 的默认值**（仍为 `ON`）：只验证它能被父工程覆盖（`set(... CACHE BOOL "" FORCE)`），
   覆盖证据见 C2。
2. **不动任何 `src/` 代码**。
3. **不导出 PUBLIC include**（C5）：父工程要 include VVOS 头仍需自己补 `-I`，或由后续单改 VVOS 的
   `target_include_directories(... PUBLIC ...)`。本单只修"VVOS 自身在父工程内能否构建"。
4. **不验证 Windows 之外的平台**（本次只在 Windows + MinGW/Ninja 上实测）。
5. **不加 `install()` / `find_package()` 支持**：当前只覆盖 `add_subdirectory()` 这一种嵌入方式。
6. **不动 `CMAKE_BINARY_DIR` 的两处使用**（`CMakeLists.txt:139` 注释、`:267` 写
   `vvos_declared_tests.txt`）：它们是同一类"顶层目录泄漏"，但**只在 `ENABLE_TESTS=ON` 且被嵌入时**
   表现为往父工程构建根写文件，属独立问题。建议后续单独评估（改 `CMAKE_CURRENT_BINARY_DIR`
   在独立构建下等价，但会改变门禁读取路径的语义，需要一起核对）。

## 父工程测试的位置

`D:/Temp/embed-test/`（`CMakeLists.txt` + `main.cpp`），**不入库**，避免把宿主工程的脚手架塞进 VVOS。
