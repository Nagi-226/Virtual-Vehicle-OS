# 证据：VVOS 嵌入能力第二层（父工程消费头文件）+ `CMAKE_BINARY_DIR` 泄漏

- 分支：`fix/cmake-export-includes`（基于 `master@c361c00`，即已合入第一层修复 `a3f0962` 之后）
- 日期：2026-11-13
- 前置：第一层（VVOS **自己在父工程内构建**）已修并合入 master，证据见
  `docs/smoke/embed-evidence.md`。本文补的是它的 **C5 遗留项**（该文件里标为"未做、待定"）。
- 改动集：**仅** `CMakeLists.txt`（1 处新增 + 1 处变量替换 + 注释），外加本证据文件。
  **未改任何 `src/` 代码，未改 `ENABLE_TESTS` 默认值。**

> 认知标记：**VERIFIED** = 附命令输出/文件行号；**INFERRED** = 给依据但未实测；**UNKNOWN** = 说明缺哪一环。
> 本文判据除 D2 的一条标注为 INFERRED 外，其余均 VERIFIED。

---

## 0. 问题（同一 bug 的第二半）

| 层 | 症状 | 状态 |
|---|---|---|
| 第一层 | VVOS **自己在父工程里构建**时找不到自己的头（`-I` 指向父工程根） | ✅ 已修（`a3f0962`，master） |
| **第二层** | 父工程**消费 VVOS 时**找不到 VVOS 的头 | **本次修** |

第二层根因：`include_directories()` 是**目录级**作用域，只影响本目录及子目录里创建的目标，
**不向父工程传播**。`vr_base_framework` 又没有 `target_include_directories(... PUBLIC ...)`，
所以父工程 `target_link_libraries(app PRIVATE vr_base_framework)` 拿不到任何 `-I`。

---

## 1. 改动

### 1.1 导出头文件目录（本层主修复）

```cmake
add_library(vr_base_framework ${VR_BASE_SOURCES})

target_include_directories(vr_base_framework
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)
```

`PUBLIC` 而非 `INTERFACE`：本库**自身编译也需要**这些头（PUBLIC 的 include 会自动参与本 target 的
编译），语义正是"用这个 target 的人也需要这些头"。**VERIFIED**（目标属性，见 C4）。

### 1.2 `CMAKE_BINARY_DIR` → `CMAKE_CURRENT_BINARY_DIR`（同类目录泄漏）

`CMakeLists.txt` 里 `vvos_declared_tests.txt` 的落盘位置（原 `:276`）：

```cmake
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/vvos_declared_tests.txt" ...)
```

原用 `CMAKE_BINARY_DIR`（**整个工程**的构建根）：被父工程 `add_subdirectory()` 嵌入时，
清单会写到**父工程构建根**，属目录泄漏。独立构建时两者相同，行为不变（**VERIFIED**，见 B2/B3）。

---

## A 组 · 构建

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| A1 | 构建通过（exit 0） | ✅ | 独立构建 `configure=0 build=0 error=0 warning=0`；嵌入构建 `BUILD_EXIT=0 errors=0` |
| A2–A4 | 烧录 / 启动 | **N/A** | VVOS 是 PC 侧库，无烧录与设备启动环节 |

---

## B 组 · 默认态零影响（复用同源目录 A/B 法）

**VERIFIED**——`git worktree add` 把 master 检出到 `/tmp/vvos-e2e`（**同一路径**），
先编 master 版 CMakeLists，再覆盖成本分支版重编，逐目标比较 `-I`：

| 目标 | 改前 `-I` | 改后 `-I` | 一致 |
|---|---|---|---|
| `error_code.cpp`（VVOS 源） | `D:/Temp/vvos-e2e/src` | `D:/Temp/vvos-e2e/src` | ✅ 逐字相同 |
| `message_codec.cpp`（VVOS 源） | `D:/Temp/vvos-e2e/src` | `D:/Temp/vvos-e2e/src` | ✅ 逐字相同 |
| `f429_bridge_diag_adapter_test.cpp` | `…/src`, `…/vvos-e2e` | `…/src`, `…/vvos-e2e` | ✅ 逐字相同 |

⇒ **新增的 PUBLIC 导出没有给 VVOS 自身的目标带来重复 `-I`**（CMake 去重），顶层构建零变化。

### B2/B3 · `vvos_declared_tests.txt` 位置

| 场景 | 位置 | 结果 |
|---|---|---|
| 独立构建（本分支，`/tmp/vvos-export-verify`） | `<build>/vvos_declared_tests.txt`，**19 行** | ✅ 与改前一致（门禁照旧能读到，见 D3） |
| 嵌入构建（`EMBED_WITH_TESTS=ON`） | `build-with-tests/vvos_build/vvos_declared_tests.txt`，19 行；**父工程构建根无该文件** | ✅ 不再污染父工程 |

> **INFERRED（唯一一条）**：`:92` 的 `-T<ld>` 与 `:256` 的 f429 include 在第一层已改为
> `CMAKE_CURRENT_SOURCE_DIR`，本次未再改动，其"零影响"依据是第一层证据（本文不重复实测）。

---

## C 组 · 关键行为验证

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| C1 | 父工程**不补 `-I`** 也能编 | ✅ | `PARENT_ADDS_INCLUDE=OFF` ⇒ `CONFIGURE_EXIT=0` / `BUILD_EXIT=0` / `errors=0` |
| C2 | 边界路径 | ✅ | 同一父工程换回 `PARENT_ADDS_INCLUDE=ON` 仍编译通过（两条路都能走，互不干扰） |
| C3 | **不是"零吞吐式通过"** | ✅ | 产物**实际运行**：`encode_ec=0 len=48` / `decode_ec=0 topic=t`，`RUN_EXIT=0` |
| C4 | 导出的是 **PUBLIC** 而非 INTERFACE | ✅ | 见下方目标属性输出 |
| C5 | 计数器与采集窗口 | **N/A** | 本单不涉及计数器 |

### C1/C3 实测（父工程 `D:/Temp/embed-test`，**不入库**）

```cmake
set(ENABLE_TESTS OFF CACHE BOOL "" FORCE)          # 或 EMBED_WITH_TESTS=ON 时 ON
add_subdirectory("E:/Github Project/Virtual-Vehicle-OS" vvos_build)
add_executable(embed_test main.cpp)
target_link_libraries(embed_test PRIVATE vr_base_framework)   # 注意：没有加任何 -I
```

```
CONFIGURE_EXIT=0
BUILD_EXIT=0
errors=0
=== 运行 ===
encode_ec=0 len=48
decode_ec=0 topic=t
RUN_EXIT=0
```

对照（第一层修完、本层未修时，即 master@c361c00 的状态）：

```
PARENT_ADDS_INCLUDE=OFF → build=1  errors=1
  D:/Temp/embed-test/main.cpp:4:10: fatal error:
  interconnect/message_codec.hpp: No such file or directory
```

### C4 目标属性（判据的精确写法）

`get_target_property(... INTERFACE_INCLUDE_DIRECTORIES)` 对 **PUBLIC 和 INTERFACE 都有值**，
所以它只能证明"消费者能拿到"，**不能区分 PUBLIC / INTERFACE**。要区分必须同时看目标的
`INCLUDE_DIRECTORIES`（本 target 自己编译用）：

```
-- [probe] INTERFACE_INCLUDE_DIRECTORIES = E:/Github Project/Virtual-Vehicle-OS/src
-- [probe] INCLUDE_DIRECTORIES(target)  = E:/Github Project/Virtual-Vehicle-OS/src;...
```

两者都有值 ⇒ **PUBLIC**（若只有前者有值、后者为空，才是 INTERFACE）。
命令行的实际效果也印证了：父工程 `main.cpp` 的编译命令里出现了
`-I"E:/Github Project/Virtual-Vehicle-OS/src"`，且**只出现一次**（无重复）。

---

## D 组 · 静态资源与依赖

| # | 判据 | 结果 | 证据 |
|---|---|---|---|
| D1 | 必需产物还在 | ✅ | 独立构建 3 个静态库：`libvr_error_code.a` / `libcommonsvc_log.a` / `libvr_base_framework.a` |
| D2 | 没有引入新依赖 | ✅ | `git diff --stat CMakeLists.txt` ⇒ `1 file changed, 21 insertions(+), 2 deletions(-)`；**无 `target_link_libraries` 变更、无新增源文件** |
| D3 | 门禁仍能读到清单 | ✅ | `declared=19 registered=19 executed=19`（门禁从 `<BuildDir>/vvos_declared_tests.txt` 读，独立构建位置未变） |
| D4 | 新运行期分配 | **N/A** | 无代码改动 |

---

## E 组 · 文档

| # | 判据 | 结果 |
|---|---|---|
| E1 | 行为变更已注明 | ✅ 两处改动都带行内注释，写明"为什么 PUBLIC 而不是 INTERFACE"、"为什么用 CURRENT_BINARY_DIR"以及各自踩过的现象 |
| E2 | 勘误旧结论 | ✅ 本文件补充了 `docs/smoke/embed-evidence.md` 的 C5 遗留项（该文件当时标为"未做、待定"），未删除旧文 |
| E3 | 未解决的边界 | ✅ 见下方 Non-Goals |

---

## 回归

```
独立构建：configure=0  build=0  error=0  warning=0
ctest   ：89% tests passed, 2 tests failed out of 19     ← 17/19，与 master 一致
失败项  ：interconnect_fault_injection_test（BL-03）/ f429_bridge_diag_adapter_test（BL-05）
门禁    ：declared=19 registered=19 executed=19
          block_reasons 与改前逐项相同，无新增红因
```

## 改动文件

```
CMakeLists.txt                        +21 −2（PUBLIC include 导出 + CURRENT_BINARY_DIR + 注释）
docs/smoke/embed-export-evidence.md   本文件
```

## Non-Goals（本次明确不做）

1. **不加 `install()` / `find_package()`**：只覆盖 `add_subdirectory()` 这一种嵌入方式；
   Qt 宿主若要 `find_package(VVOS)` 需另一单。
2. **不动任何 `src/` 代码**。
3. **不改 `ENABLE_TESTS` 默认值**（仍 `ON`）。
4. **不给 `commonsvc_log` / `vr_error_code` 单独导出 include**：它们是内部库，
   公有入口是 `vr_base_framework`（它 PUBLIC 链接这两个）。若将来有人只链 `commonsvc_log`，
   仍需自己补 `-I` —— 属"内部库被外部直接使用"的异常用法，不在本单范围。
5. **不验证 Windows 之外的平台**（本次仅 Windows + MinGW/Ninja 实测）。
6. **不改父工程侧的 `-I` 写法**（`D:/Temp/embed-test` 是临时脚手架，不入库）。

## 父工程测试的位置

`D:/Temp/embed-test/`（`CMakeLists.txt` + `main.cpp`），**不入库**。
本次为其加了两个开关：`BUILD_PARENT_CONSUMER`、`PARENT_ADDS_INCLUDE`、`EMBED_WITH_TESTS`，
以便同一份脚手架分别验证"父工程补/不补 -I"和"嵌入时测试开/关"两种组合。
