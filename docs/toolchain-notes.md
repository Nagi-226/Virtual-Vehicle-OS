# 工具链注意事项（Toolchain Notes）

本文件记录**已被实测确认**的工具链/平台陷阱，避免同一个坑被反复踩。

- 适用环境：Windows + MinGW-w64 / GCC 15.1.0（`g++.exe (Rev7, Built by MSYS2 project)`）
- 记录时间：2026-11-13
- 发现来源：`tests/log/logger_format_test.cpp` 在 ctest 中以 `0xC0000374` 崩溃

> ⚠️ **路径约定**：文中出现的盘符绝对路径（`D:/…`、`C:` 盘下的标准安装目录等）**均来自一台具体开发机，仅作示例**。
> 换机器时请按自己的安装位置替换，**不要把它们当成必须存在的路径**。
> （本文保留具体路径而不泛化成「安装根」，是因为这些坑的成因常与**具体目录结构**有关，抹掉就失去可复现性。）

---

## TN-01 · 非平凡析构的 `thread_local` 对象在线程退出时崩溃

### 1. 现象

进程以 **`0xC0000374`（`STATUS_HEAP_CORRUPTION`）** 直接终止，**没有任何 C++ 异常、
没有可读的错误信息**。崩溃发生在线程退出的析构阶段，而不是在业务调用处——如果线程里
的日志/工作已经"看起来成功完成"，会给人"跑完了才崩"的错觉。

典型触发形态：

```cpp
thread_local std::string g_module;   // 非平凡析构

void SetCtx(const std::string& m) { g_module = m; }

int main() {
    std::thread t([]() {
        SetCtx("worker_module");
        std::cout << g_module << std::endl;   // 这一行正常输出
    });
    t.join();   // ← 崩在这里，或更早：线程退出时执行 TLS 析构
}
```

### 2. 最小复现（20 行，无任何业务代码）

把下面这段单独存成 `repro.cpp` 即可复现（`g++ -std=c++17 -O0 repro.cpp`）：

```cpp
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

thread_local std::string g_module;
thread_local std::string g_context;

void SetCtx(const std::string& m, const std::string& c) {
    g_module = m;
    g_context = c;
}

int main() {
    std::thread t([]() {
        SetCtx("worker_module", "worker_ctx");
        std::cout << "worker wrote: " << g_module << "/" << g_context << std::endl;
    });
    t.join();
    std::printf("joined\n");
    return 0;
}
```

实测输出：
```
worker wrote: worker_module/worker_ctx
（进程退出码 0xC0000374，未打印 joined）
```

### 3. 定位方法（ASan 不可用时的替代手段）

本机 **无法使用 ASan**：

| 路线 | 结果 |
|---|---|
| MinGW g++ `-fsanitize=address,undefined` | 链接失败 `ld: cannot find -lubsan`；整棵工具链树里**没有 libasan/libubsan** |
| MSVC 2022 BuildTools `/fsanitize=address` | 有 ASan 运行库，但 BuildTools 缺 Windows SDK 头（`crtdbg.h`），且 `vcvars64.bat` 内部会调用被沙箱黑名单拦截的 `reg.exe` |

替代方案（本仓库实际使用，可复用）：

1. **自建金丝雀分配器**：覆盖全局 `operator new/delete`，在每次分配前后各加 16 字节
   守卫 + 8 字节尺寸头，释放时校验；踩到守卫就打印尺寸、偏移、最近 N 次堆事件并 `abort()`。
   约 150 行，能抓到"越界写 / 前缀越界 / 重复释放"。
2. **`addr2line` 解栈**：用 `-Wl,--disable-dynamicbase` 关掉 ASLR，
   在失败点用 `CaptureStackBackTrace` 打印地址，再用
   `addr2line -e xxx.exe -f -C -i <addr...>` 还原到"文件:行"。

本缺陷由第 2 条手段定死：
```
~basic_string  ←  run_dtor_list  ←  tls_callback
（D:/W/B/src/mingw-w64/mingw-w64-crt/crt/tls_atexit.c:63 / :56 / :165）
```
即：**TLS 析构列表里的 `std::string` 析构函数执行在一个已经被拆掉的 TLS 存储上**，
读到的野指针被拿去 `free`。

### 4. 变体矩阵（同一台机器 / 同一 GCC）

| 变体 | 结果 |
|---|---|
| `thread_local std::string` + 工作线程 + **动态** libstdc++ | 不崩 |
| `thread_local std::string` + 工作线程 + **静态** libstdc++（`-static-libstdc++`） | **崩 `0xC0000374`** |
| `thread_local std::string` + 工作线程 + 一次 `std::cout` 写 | **崩** |
| `thread_local std::string` 只赋 SSO 短串、不碰 iostream | 不崩 |
| `thread_local std::string` 只在**主线程**使用 | 不崩 |
| `thread_local int`（平凡析构） | 不崩 |

**结论**：只要"非平凡析构 + 被非主线程使用 + 线程退出"，就落在危险区；
是否崩溃还受 libstdc++ 链接方式、是否碰 iostream 等次要因素影响，所以**不要依赖
"我这版没崩"来判断安全**。

### 5. 规则：项目内禁止非平凡析构的 `thread_local`

- **禁止**：`thread_local std::string` / `std::vector` / `std::map` / 任何带非平凡析构的类型
  （包括自定义类、`std::function`、`std::shared_ptr` 等）。
- **允许**：平凡析构类型 —— `bool` / 整数 / 浮点 / 指针 / **定长数组**。
- **推荐替代**：
  - 需要字符串 → `thread_local char buf[N]`（定长缓冲，超长截断，见
    `src/log/logger.cpp` 的 `kThreadContextCapacity`）
  - 需要复杂对象 → 用 `thread_local` 的**指针**并刻意不释放（有明确注释与容量上界），
    或改用"按 `std::thread::id` 索引的受锁表"
- **加锁顺序**：若改用受锁表，统一约定"线程上下文表锁 → `output_mutex_`"，不得反向。

### 6. 现有唯一实例已修

`src/log/logger.cpp` 线程上下文原本是三个 `thread_local`（两个 `std::string`），
现已改为：

```cpp
thread_local bool g_has_thread_context = false;
thread_local char g_thread_module[kThreadContextCapacity] = {0};   // 平凡析构
thread_local char g_thread_context[kThreadContextCapacity] = {0};  // 平凡析构
```

公开 API 未变（仍是 `const std::string&` 入参、`std::string*` 出参），代价只是超长名截断。

### 7. CI 防回归

`tools/run_quality_gate.ps1` 已加入静态检查 `Find-ForbiddenThreadLocals`：
扫描 `src/`、`tests/`、`demo/` 下所有源文件，`thread_local` 声明的类型必须命中白名单
（`bool`/整型/浮点/`char`/`std::size_t`/`std::intN_t`/`std::uintN_t`），否则门禁失败并给出
`thread_local_nontrivial_forbidden:<文件>:<行>`。

已知局限（务必了解）：

- 这是**启发式单行检查**，跳过 `//`、`*`、`/*` 开头的行，因此跨行的块注释里提到
  `thread_local` 不会被误报，但**多行声明**（类型名换行）也可能漏报。
- 它**不能替代 code review**：真正的兜底是"新加 `thread_local` 必须过脑子"。
- 建议在 review 清单里加一条：「新增 `thread_local`？类型是否平凡析构？」

---

## TN-02 · 规格里的"逐字节向量"必须先过一遍真实现，再动手

**规则**：只要一份规格/设计文档里出现"逐字节比对""测试向量""断言字节数"这类内容，
**先用仓库里的真实现把向量跑一遍**，确认「规格说的字节」和「真代码产出的字节」一致，
再开始写实现。不要只核规格作者的算术。

### 为什么（这条是实际救过时间的，不是理论）

2026-11-13 做 U03（MCU 侧 wire codec）规格复核时，用真 `MessageCodec::Decode` 跑了规格里
5 个向量，同一个动作同时产出了三类结论——**任何一类单独看都不会被发现**：

| 类别 | 例子（当次实际结果） |
|---|---|
| ① 规格的数字对不对 | 向量 1 文本 110 B、向量 2 106 B、payload 43 B —— 全对（作者把 42 改成 43 是改对了） |
| ② 规格与实现在**语义**上是否错位 | 8 字段顺序 `[0]schema…[7]payload` 与 `message_codec.cpp:194-197`、`:214-219` 逐项一致，没有错位 |
| ③ **真实现自身的缺陷** | 向量能过，但顺手试 `sequence=0x1F` 发现被静默解析成 `0`（`std::stoul` 只解析前缀）——这是规格作者的"算术正确"完全覆盖不到的一层 |

第 ③ 类最值钱：**规格通过 ≠ 实现正确**。规格只描述"正确输入应该产生什么"，
而缺陷几乎都长在"错误输入被静默接受"这一侧。

### 具体做法（可复用）

1. 把规格里的向量原文抄进一个临时探针程序（**不入库**，放 `/tmp` 或 `D:/Temp`）。
2. 在探针里调用**仓库真实模块**（不要重写一份等价逻辑——重写就把被测对象换掉了）。
3. 断言三件事：字节长度、解码后的每个字段值、**以及负向输入必须被拒绝**。
4. 顺带把"畸形输入"当一等公民：非十进制、带尾垃圾、空字段、超长值、`+5`、前导空格。
5. 结论分三类写进报告：规格对不对 / 有没有语义错位 / 真实现有没有缺陷。

### 反例（只核算术会漏掉什么）

同一份规格里，"`std::stoul` 不检查是否消费完整个字段"这条是**规格作者自己写对的**，
但他把它写成了"MCU 侧必须只发十进制来回避"——即把缺陷固化成使用约束。
如果只核"向量能不能过"，这条会永远留在接收侧，症状是"偶发丢包 + 归因到 TTL 策略"。
真实现跑一遍才会让人想去看 `stoul` 的第二个参数。

---

## 附二：ESP-IDF v5.2.7 环境三坑（由 software-engineer-2 真机实测提供）

如果要做 RoboMind-S3 侧的交叉构建，这三条会各花掉半小时以上，且**报错信息都具有误导性**：

| # | 现象 | 真实原因 | 绕过方式 |
|---|---|---|---|
| 1 | `export.ps1` 输出到 `Adding ESP-IDF tools to PATH...` 就**静默退出（exit 9009）**，PATH 完全不变，看起来像"IDF 装坏了" | 该脚本本身有问题，不是安装损坏 | `python tools/idf_tools.py export --format key-value` 自己解析并注入环境 |
| 2 | `idf.py build` 跑到 `[633/644]` 后 ninja **空转 20 分钟**（CPU 累计只涨 9.9 s、没有编译器子进程） | 4 个 `ccache.exe` 子进程挂住 | 一律使用 `idf.py --no-ccache build` |
| 3 | `idf.py size` / `size-components` 直接崩：`TypeError: size_target() missing 1 required positional argument: 'legacy'` | 5.2 的 size 子命令签名不兼容 | `python tools/idf_size.py [--archives] build/robo_mind_s3.map` |

> 另注：`git bash` 下跑 `idf.py` 前需先完成 export（见第 1 条），否则 `idf.py` 与
> `xtensa-esp32s3-elf-gcc` 都不在 PATH —— 这类"工具不存在"的错误只是没加载环境，
> 不代表工具链缺失。

---

## 附：本机环境速查

| 项 | 值 / 结论 |
|---|---|
| 编译器 | `g++.exe (Rev7, Built by MSYS2 project) 15.1.0`，`/e/mingw64/bin` |
| CMake / Ninja | 3.29.0 / 1.13.2 |
| ASan / UBSan | **不可用**（无 libasan / libubsan） |
| gdb | **未安装** |
| MSVC | VS2022 BuildTools 14.44 在 `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`，**缺 Windows SDK**，且 `vcvars64.bat` 会调用被沙箱拦截的 `reg.exe` |
| Application Verifier | `C:\Windows\system32\appverif.exe` 存在（未使用，page heap 需管理员） |
| ESP-IDF | v5.2.7 在 `D:/Espressif/frameworks/esp-idf-v5.2.7/`，**需先加载 export 脚本**才有 `idf.py` / `xtensa-esp32s3-elf-gcc` |
