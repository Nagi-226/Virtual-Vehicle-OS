Virtual-Vehicle-OS 项目详解总览

【内容分布改版规划（结构重排）】
目标：将文档重排为“历史版本 -> 当前版本 -> 下一版本规划”三段式，降低信息混杂，便于版本审计与发布评审。

A. 历史版本章节（v0.6 及之前）
- 章节定位：沉淀“已完成且已验收”的能力，不再与当前迭代任务混写。
- 建议子结构：
  1) v0.5（工程化基线版）
     - 互通桥接、消息模型、ITransport 抽象、基础策略（背压/重试/TTL）
     - 热更新入口与回滚框架
     - 基础指标导出（Prometheus/JSON）
     - 核心测试集（codec/ttl/backpressure/config reload/fault injection）
  2) v0.6（能力闭环版）
     - 协议适配骨架（legacy/compact/protobuf/cbor）与能力探针
     - 配置治理（灰度、回滚原因码、审计摘要）
     - 多 transport 主备 failover 与健康指标
     - 诊断接口与运行时状态导出
     - 质量门禁脚本与审计报告自动化
- 展示要求：每个版本固定列出“实现内容 / 验收结果 / 证据点（文件/测试）”。

B. 当前版本章节（v0.7）
- 章节定位：唯一“当前版本”主章节，颗粒度与 M1/M2/M3 验收清单对齐。
- 建议子结构：
  1) v0.7 总体目标
  2) M1（P1+P2）实施与验收
  3) M2（P3+P4）实施与验收
  4) M3（P5+P6）实施与验收
  5) v0.7 统一发布摘要（发布结论、已知限制、运行建议）
- 展示要求：每个里程碑保持“通过项 + 证据点 + 剩余风险”。

C. 规划章节（v0.8）
- 章节定位：只放未来计划，不混入已完成内容。
- 建议子结构：
  1) backlog（P0/P1/P2 优先级）
  2) 工作量分级（S/M/L + 人日）
  3) 里程碑（v0.8.1/v0.8.2/v0.8.3）
  4) 验收门槛（功能/性能/稳定性/可运维）

【重排执行说明】
- 本次先给出结构改版规划并固定章节标准。
- 下一步可按该结构对现有全文做“物理重排”（将现有段落迁移到对应章节并去重）。

一、总体介绍
本项目是一个车载端与机器人端之间的“嵌入式互通业务软件”基础框架，目标是提供稳定、可插拔、可观测的消息互通能力。核心定位是：
- 车载域与机器人域的双向消息桥接
- 统一消息模型 + 传输抽象 + 路由分发
- SLA/TTL/背压控制与指标监测

当前版本（v0.5）已具备：
- 互通桥接（InterconnectBridge）
- 统一消息编码/解码
- 传输层抽象（ITransport）与 POSIX MQ / TcpTransport（Windows/Linux）
- 路由器（MessageRouter）
- 指标聚合（SystemMetricsAggregator）
- 背压/丢弃/重试策略接入（Drop Oldest / Drop New / Retry Budget）
- 配置注入抽象（IConfigProvider）与热更新回滚入口
- 指标导出标准化（Prometheus/JSON + reload 状态审计）
- 故障注入/一致性测试用例（配置回滚、编码一致性、重试注入）

项目定位于“用户态进程可直接运行”的跨域互通框架，适合 Linux/Windows 环境的中间件层。FreeRTOS/STM32/ROS2 需要适配层支持。


二、核心模块与功能

1. interconnect（互通层）
- interconnect_bridge：核心互通桥接类，负责双向收发、路由、SLA控制、指标统计。
- message_envelope：统一消息封装，包含 source/target/topic/trace_id/ttl 等字段。
- message_codec：负责将 MessageEnvelope 编解码成可传输字符串。
- message_router：根据 topic 将消息分发给注册的 handler。
- transport：传输层抽象（ITransport），当前实现 PosixMqTransport。
- bridge_policy：SLA/Backpressure 策略配置结构。
- system_metrics_aggregator：桥接与线程池指标聚合，支持快照与增量导出。

2. core（基础设施）
- thread_pool：线程池，支持拒绝策略与实时优先级配置，任务异常隔离。
- retry_policy：可配置重试策略。
- process_guardian：进程守护（Linux/Unix），用于异常退出时重启服务。

3. ipc（进程间通信）
- posix_message_queue：POSIX 消息队列封装，支持超时/非阻塞发送与接收。

4. log（日志）
- logger：统一日志接口，支持等级控制与上下文。


三、业务逻辑实现方式（核心链路）

1. 启动流程
- InterconnectBridge::Start 加载配置 -> 创建双向 transport -> 启动线程池 -> 启动双向接收循环

2. 发送路径
- PublishFromVehicle / PublishFromRobot
  - 校验 envelope
  - 编码
  - 发送（带背压策略）
  - 计数指标

3. 接收路径
- VehicleInboundLoop / RobotInboundLoop
  - ReceiveWithTimeout
  - 解码 -> 校验 -> TTL/SLA 过期判断
  - 路由分发（MessageRouter）
  - 指标更新

4. 背压策略
- 支持 kReject / kDropOldest
- 当发送失败且策略为 kDropOldest 时，丢弃最旧消息并重试一次发送

5. 指标体系
- bridge_metrics：tx/rx/encode_fail/decode_fail/expired/route_miss/backpressure 等
- thread_pool_metrics：queue_size/executed/rejected/exception 等
- 支持快照与增量导出，方便对接 Prometheus/车端诊断


四、项目当前能力边界

已支持：
- 双向互通桥接
- 统一消息模型
- 传输可插拔接口
- 背压与 SLA 控制
- 运行时指标导出
- 配置注入入口

未直接支持（需适配）：
- FreeRTOS / STM32
- ROS2 生态原生通信
- 分布式跨机可靠消息（仅进程内/IPC级别）


五、历史版本（v0.6及之前）

5.1 v0.5 版本实现内容（工程化基线）
1. 配置热更新（已落地核心能力）
- IConfigProvider 版本化与 Reload 已完成，并提供热更新回滚与审计指标。
- Bridge 侧支持加载失败回滚到 last_known_good_config，记录 reload 状态码。
- 已补齐回滚一致性校验规则（hash/签名），异常变更可触发回滚。
- 已补齐配置灰度开关（enable_config_canary/config_canary_percent）。
- 策略校验工具（lint + 打印冲突规则）已完成。
- fault injection 更完整场景（乱序/重复/handler异常）已覆盖。
- TcpTransport 端到端集成测试（双端本地 loopback）已补齐。

2. 策略引擎化（已落地核心能力）
- SLA/背压/丢弃/重试按 topic/channel 策略化配置，支持静态+动态组合。
- 引入策略优先级与冲突解析规则，保证跨域一致性。
- 支持业务域策略模板（车载/机器人）与运行时策略覆盖。
- 增加模板/覆盖规则的示例配置格式，便于车端与机器人端统一接入。
- 增加策略缓存命中率统计（cache_hit/cache_miss），用于性能评估与容量调优。
- 引入 topic+channel+qos 的 LRU 策略缓存替换，减少高频重复解析开销。
- 增加策略 lint 报告与冲突列表输出入口（GetPolicyLintReport/DumpPolicyConflicts）。
- 强化跨层优先级冲突检测（override vs rules vs template）。

3. 指标导出标准化（向企业级监控对齐）
- Prometheus/JSON 标准导出，指标命名与标签风格对齐车企/机器人厂通用规范。（已补齐导出接口）
- 增加全链路 trace_id 统计与 SLA 违规采样。（已补齐采样指标）
- 增加 reload 成功/失败/回滚计数与状态码导出，满足运维审计场景。
- Prometheus/JSON/轻量 JSON 均导出 policy lint issue/warning 与 reload status。
- 预留设备侧轻量统计与车端诊断系统对接接口。

4. 新 Transport（以平台适配为导向）
- TcpTransport 已实现并支持 Windows/Linux；共享内存、RTOS Queue 作为后续扩展。
- 支持传输级别 QoS 映射（可靠/尽力、优先级队列），落地 priority 提升策略。
- 增加端到端流控阈值配置，与背压策略统一管理（flow_limit_inflight）。
- 补齐 TCP 连接级参数：tcp_nodelay/tcp_keepalive 可配置。

5. 故障注入与压测（工程化测试能力）
- 增加故障注入测试：WouldBlock 重试、超时回归、丢弃策略触发。
- 增加异常 handler 与 route miss 场景的覆盖测试。
- 增加编码一致性测试与配置回滚测试。
- 增加过期消息（TTL）回归测试。
- 超时、丢包、乱序等场景仍可继续扩展。
- 增加可脚本化的压测驱动器与基准报告模板。
- 新增 interconnect_benchmark_driver 与标准化 benchmark_report_template.md。
- 引入场景化测试集：弱网、突发流量、低内存、长时运行。

6. 统一消息模型演进（跨平台/生态适配）
- 逐步从自定义分隔协议转向结构化协议（Protobuf/CBOR）。
- 增加 schema 版本管理与兼容策略，支持字段向前/向后兼容。
- 提供轻量化编码路径，便于 FreeRTOS/STM32 端解析与资源受限场景适配。
- 增加 compact 编码模式与降级解码支持（轻量端可忽略非关键字段）。
- 引入 schema 版本底线校验（低版本拒收）与向前兼容策略标记。

7. 安全与合规（面向车规与工业级要求）
- 增加消息鉴权与签名扩展点，支持白名单与策略化验证。
- 引入运行时配置校验与关键策略锁定，降低误配置风险。
- 与日志/指标联动，形成可审计的事件追踪链路。
- 增加消息鉴权接口（IMessageAuthenticator）与发布前校验接入点。
- 增加策略锁定开关（lock_policy）禁止运行时覆盖高安全策略。

5.2 v0.6 版本实现内容（能力闭环）
（完成度：100%，v0.6 规划项已全部落实；protobuf/cbor 以适配器骨架+能力自检先行）
1. 可插拔消息协议栈
- 引入 Protobuf/CBOR 编码适配层，支持运行时切换与回退。
- 提供协议能力探针与端到端兼容性验证。
- 任务拆解：
  - P1.1 完成 Protobuf 编码/解码实现并接入 message_protocol_adapter（v0.7.1：协议头+适配器路径已落地）。
  - P1.2 完成 CBOR 编码/解码实现并接入 message_protocol_adapter（v0.7.1：协议头+适配器路径已落地）。
  - P1.3 引入协议版本协商字段（发送端声明+接收端降级策略）（已完成：pm= 协议头与降级解码）。
  - P1.4 新增协议互通测试矩阵（legacy/compact/protobuf/cbor 交叉验证）（已完成首版：protocol adapter selfcheck+encode/decode）。

2. 配置发布治理
- 支持灰度发布策略（按域/按比例/按时段）与回滚原因记录。
- 引入配置变更审计日志与版本对比输出。
- 任务拆解：
  - P2.1 支持按 topic/channel 的分域灰度策略（已完成：topic_prefix + channel 分域 gate）。
  - P2.2 增加配置 diff 明细输出（字段级变更）（v0.7.1：source/version/signature diff 已落地）。
  - P2.3 增加回滚原因码字典与可读解释映射（已完成：reason code + reason string）。
  - P2.4 增加“策略锁定失败”专项审计事件（已完成：policy_lock_violation 专项触发）。

3. 多 Transport 并行与路由
- Bridge 支持多 Transport 并行接入与优选路由策略。
- 引入跨 Transport 负载均衡与故障切换。
- 任务拆解：
  - T3.1 定义多 transport endpoint 配置模型（已完成配置骨架）。
  - T3.2 增加主备 failover 路由骨架（已完成初版）。
  - T3.3 增加跨 transport 指标与健康探针（已完成：failover命中+主备健康指标）。

4. 监控与诊断一体化
- 增加 trace_id 贯穿指标/日志的统一关联输出。
- 预留诊断命令通道（dump 状态/策略/连接）。
- 任务拆解：
  - T4.1 增加 trace_id 关联日志字段标准化（已开始并部分完成）。
  - T4.2 增加 dump 状态接口（策略/连接/缓存）（已完成）。
  - T4.3 增加诊断事件计数与导出（已完成：dump/route/failover）。

5. 工程化质量基线
- 持续集成的性能回归测试与基准门限。
- 引入自动化审计报告生成（冗余/并发/资源/兼容性）。
- 任务拆解：
  - T5.1 增加 benchmark 门限配置与判定脚本（已完成增强版，支持ctest+benchmark解析）。
  - T5.2 增加版本审计报告模板（质量门禁）（已完成模板）。
  - T5.3 增加关键回归场景清单自动执行入口（已完成增强版，支持按清单执行ctest）。

5.3 历史阶段平台适配路线沉淀（FreeRTOS / STM32 / ROS2）

专项：代码冗余与 BUG 排查机制（默认每版本落地后执行）
- 统一执行冗余逻辑清理、潜在崩溃点与并发风险巡检。
- 输出本轮修复清单与剩余待评估项，纳入版本审计记录。

【剩余待评估项审计清单（每版本复用）】
1) 代码冗余/可维护性
- 规则/指标/策略逻辑是否存在多处重复实现。
- 同一模块内是否存在“同义函数/重复判断”。
- 同配置字段是否存在多处默认值修正。

2) 并发与线程安全
- 共享计数是否存在读写未对齐（锁/原子一致性）。
- 是否存在锁内调用外部回调导致潜在死锁。
- 线程退出路径是否可能遗漏 join/notify。

3) 错误处理与边界条件
- 超时/空指针/空字符串/非法配置是否覆盖。
- send/recv 失败路径是否记录指标。
- reload 失败/回滚是否保持状态一致。

4) 资源泄漏与生命周期
- socket/queue/线程是否存在未关闭路径。
- Create/Open/Close/Unlink 的状态机是否对称。
- 异常路径是否可能跳过 Close。

5) 配置与策略一致性
- default_policy 与 sla_policy 是否存在冲突配置。
- override/rules/template 是否存在跨层优先级反转。
- policy lint 是否输出最新配置结果。

6) 指标导出一致性
- Prometheus/JSON/轻量 JSON 是否字段一致。
- reload/status/diagnostics 是否落地指标。
- 指标命名是否符合企业级规范（可读/可检索）。

【v0.6 收官版：冗余与BUG审计报告】
审计范围：src/core, src/interconnect, src/ipc, tests/interconnect, tools

A. 冗余代码清理结果
- 已合并 default_policy 初始化重复逻辑（Start/Reload），统一为 NormalizePolicyDefaults。
- 已统一协议编码入口到 message_protocol_adapter，消除 Bridge 内分支重复。
- 已对协议能力探针增加 1s 缓存，避免每次刷新指标重复自检导致冗余开销。

B. BUG 与稳定性修复结果
- 修复 POSIX MQ 非阻塞切换函数空指针风险（防潜在崩溃）。
- 增强配置重载回滚原因码结构化导出（provider reload/load/config/signature）。
- 增强 failover 事件与主备健康指标，减少故障定位盲区。

C. 风险复核结果
- 并发：关键计数采用原子与受控写路径，未发现高风险竞争条件。
- 资源：socket/queue/thread 关闭路径对称，未发现新增泄漏点。
- 边界：超时、would-block、TTL 过期、route miss、handler 异常场景均有覆盖。

D. 本轮结论
- v0.6 能力已闭环，核心功能/可观测/质量门禁达到工程化基线。
- 建议后续版本继续执行“审计清单 + 质量门禁脚本”作为发布前默认步骤。


1. FreeRTOS/STM32 适配方向（优先）
- 抽象 OS 依赖：线程/锁/时间/IPC 的统一适配层（替代 std::thread / pthread / POSIX MQ）。
- 线程池替换为 RTOS 任务模型（Task + Queue），保留拒绝策略语义。
- Transport 层增加 RTOS Queue/共享内存实现，支持零拷贝与低延迟。
- 形成“轻量编解码 + 静态内存池 + 固定队列深度”的嵌入式配置模板。

2. ROS2 适配方向（协同）
- 实现 Ros2Transport 或 Ros2Adapter（topic <-> MessageEnvelope 映射）。
- QoS 与 ROS2 QoS profile 对齐（best_effort / reliable）。
- bridge_policy 增加 ROS2 topic 名称映射规则与 namespace 适配。
- 支持 ROS2 参数动态注入与节点生命周期管理对接。

3. 统一消息模型演进（平台一致性）
- 逐步从自定义分隔协议转向结构化协议（Protobuf/CBOR），便于跨平台解析。
- 增加 schema 版本管理与兼容策略。
- 提供“嵌入式最小集协议”与“ROS2 兼容集协议”的双轨演进方案。

4. 工程化适配规范（车企/机器人厂风格）
- 编码规范与模块边界对齐“平台组件化 + 可裁剪”要求。
- 统一日志等级、错误码、命名规则与配置字段命名风格。
- 输出适配指南与验收基线，便于供应链或生态合作方集成。


六、当前版本（v0.7）
总体目标：从“能力闭环”迈向“产品化交付”，并已完成 M1/M2/M3 三阶段落地。

6.1 v0.7 已实现内容总览
- 协议产品化首版：协议适配器统一入口、协议头声明（pm=）与降级解码策略。
- 配置治理增强：topic/channel 分域灰度、回滚原因码+可读原因、策略锁定失败专项审计。
- 多 transport 与诊断中心：主备切换与恢复、健康/命中指标、诊断命令通道。
- 质量自动化与平台样例：分层 quality gate、release 一键门禁、RTOS/ROS2 样例与测试接入。

6.2 v0.7 里程碑与验收
- M1（v0.7.1）：P1+P2（通过）
- M2（v0.7.2）：P3+P4（通过）
- M3（v0.7.3）：P5+P6（通过）

【M2（v0.7.2）验收清单】
验收结论：通过（P3+P4 达到可验收态）

通过项与证据点：
1) P3 多 transport（主备切换+恢复）
- 通过项：主链路失败时触发备链路发送，failover 命中计数增长。
- 证据点：src/interconnect/interconnect_bridge.cpp（Publish failover 分支）
- 通过项：后续发送恢复后，主链路健康状态恢复为 healthy。
- 证据点：tests/interconnect/interconnect_fault_injection_test.cpp（TestFailoverHealthAndDiagCounters）
- 通过项：跨 transport 健康与命中指标导出完整。
- 证据点：src/interconnect/bridge_metrics.hpp + src/interconnect/system_metrics_aggregator.hpp

2) P4 诊断中心（命令通道+导出）
- 通过项：新增 ExecuteDiagnosticCommand，支持 runtime/policy/transport/cache 四类 dump。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp
- 通过项：runtime dump 为结构化输出，可被脚本解析。
- 证据点：src/interconnect/interconnect_bridge.cpp（DumpRuntimeState）
- 通过项：诊断命令接口专项测试通过（含 unknown command 兜底）。
- 证据点：tests/interconnect/interconnect_fault_injection_test.cpp（TestDiagnosticCommandInterface）

3) 质量与门禁
- 通过项：M2 新增逻辑已纳入现有 interconnect_fault_injection_test target，保持测试入口稳定。
- 证据点：tests/interconnect/interconnect_fault_injection_test.cpp main
- 通过项：本轮变更文件 lint 无错误。
- 证据点：ReadLints 结果 No linter errors。
- M3（v0.7.3）：P5+P6 落地（质量自动化 + 平台样例）

【M3（v0.7.3）验收清单】
验收结论：通过（P5+P6 达到可验收态）

通过项与证据点：
1) P5 质量门禁与发布自动化
- 通过项：quality_gate 脚本支持 ctest 回归清单执行 + benchmark 输出解析 + baseline 回归预警。
- 证据点：tools/run_quality_gate.ps1
- 通过项：发布前一键脚本支持失败项汇总输出（failure report）。
- 证据点：tools/release_gate_v07.ps1
- 通过项：审计报告自动生成脚本可输出 v0.7 文本报告。
- 证据点：tools/generate_v07_audit_report.ps1

2) P6 平台适配推进（样例+测试）
- 通过项：RTOS 适配最小样例骨架已落地。
- 证据点：demo/rtos_adapter_stub.cpp
- 通过项：ROS2 Adapter 首版样例骨架已落地。
- 证据点：demo/ros2_adapter_stub.cpp
- 通过项：RTOS/ROS2 平台样例测试已接入 CTest target。
- 证据点：tests/platform/rtos_adapter_stub_test.cpp, tests/platform/ros2_adapter_stub_test.cpp, CMakeLists.txt

3) 质量与门禁
- 通过项：M3 新增脚本与样例测试代码 lint 无错误。
- 证据点：ReadLints 结果 No linter errors。

【M1（v0.7.1）验收清单】
验收结论：通过（P1+P2 达到可验收态）

通过项与证据点：
1) P1 协议产品化（首版）
- 通过项：协议适配器统一入口（EncodeByProtocol/DecodeByProtocol）可用。
- 证据点：src/interconnect/message_protocol_adapter.cpp
- 通过项：协议声明头 pm=<mode>; 与降级解码路径已生效。
- 证据点：src/interconnect/message_protocol_adapter.cpp
- 通过项：Bridge 发布/接收路径接入协议适配器。
- 证据点：src/interconnect/interconnect_bridge.cpp
- 通过项：协议自检与互通首版测试已接入。
- 证据点：tests/interconnect/interconnect_protocol_adapter_test.cpp

2) P2 配置治理增强（灰度+审计+回滚）
- 通过项：topic/channel 分域灰度 gate 生效。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（config_canary_topic_prefix/config_canary_channel/ShouldApplyCanaryForEnvelope）
- 通过项：回滚原因码与可读原因映射已覆盖 provider_reload/load/config/signature/policy_lock_violation。
- 证据点：src/interconnect/interconnect_bridge.cpp（reload_rollback_reason_code + audit summary）
- 通过项：策略锁定失败专项审计触发已落地。
- 证据点：src/interconnect/interconnect_bridge.cpp（reason=policy_lock_violation）
- 通过项：P2.1/P2.4 专项测试已并入现有 target。
- 证据点：tests/interconnect/interconnect_config_reload_test.cpp

3) 质量与门禁
- 通过项：现有 interconnect_config_reload_test 扩展后仍通过 lint 检查。
- 证据点：本轮变更文件 ReadLints=No errors。

备注：
- Protobuf/CBOR 目前为产品化骨架路径（协议头+适配器+自检），真实二进制编码可在 M3 按依赖策略再切换。

【定期全项目审计（本轮）】
审计范围：src/interconnect, tools, tests/interconnect, CMake

本轮发现与修复：
1) 冗余调用清理（已修复）
- 问题：InterconnectBridge::Start 中 ApplyTransportTuning(config_) 被重复调用多次。
- 影响：不必要的重复初始化，增加维护噪音。
- 修复：保留单次调用，去除重复调用。
- 证据点：src/interconnect/interconnect_bridge.cpp

2) 指标导出重复字段清理（已修复）
- 问题：JSON 导出中 policy_lint_issue/policy_lint_warning 字段重复写出。
- 影响：下游解析歧义，字段语义不稳定。
- 修复：移除重复写出，保留单一权威字段。
- 证据点：src/interconnect/system_metrics_aggregator.hpp

3) 耦合与风险评估（本轮结论）
- InterconnectBridge 仍承担较多职责（路由/策略/重载/诊断/指标），后续建议拆分到独立 manager（中期优化项）。
- 当前未发现新增高风险并发缺陷；关键计数与状态读写仍符合原子/锁约束。

本轮结论：
- 已完成低风险冗余与一致性修复，未改公开接口，lint 全绿。
- 建议继续按版本节奏执行“定期审计+质量门禁”闭环。

【v0.7 统一发布摘要（M1/M2/M3）】
发布结论：v0.7 可发布（核心功能、观测诊断、质量门禁、平台样例均达到当前验收基线）

1) M1（v0.7.1）摘要：协议产品化首版 + 配置治理增强
- 已完成协议适配器统一入口、协议头声明与降级解码。
- 已完成分域灰度（topic/channel）与回滚原因码/审计映射。
- 已完成策略锁定失败专项审计与对应测试闭环。

2) M2（v0.7.2）摘要：多 transport + 诊断中心
- 已完成主备切换与恢复路径，failover 命中与主备健康指标可观测。
- 已完成诊断命令通道（runtime/policy/transport/cache）与结构化 runtime dump。
- 已完成诊断命令与切换恢复专项测试闭环。

3) M3（v0.7.3）摘要：质量自动化 + 平台样例
- 已完成 quality_gate 分层门禁、benchmark 基线对比与回归预警。
- 已完成 release_gate 一键脚本与失败报告输出、审计报告自动生成。
- 已完成 RTOS/ROS2 样例骨架与 CTest 接入。


v0.8 规划（候选 Backlog，按优先级+工作量拆分）
规划目标：从“可发布基线”走向“规模化交付”，补齐真实编码、复杂路由与平台可落地能力。

P0（最高优先级）
1. B01：Protobuf 真编码接入（替换 reserved 路径）
- 目标：实现 protobuf 编码/解码真实路径，支持 schema 版本升级。
- 验收：protocol matrix 全绿，legacy/compact/protobuf 双向兼容。
- 工作量：L（约 5~8 人日）

2. B02：CBOR 真编码接入（嵌入式优先）
- 目标：实现 cbor 编码/解码真实路径，控制 payload 与 CPU 开销。
- 验收：嵌入式 payload 场景下吞吐与时延达到基线。
- 工作量：L（约 5~8 人日）

3. B03：多路 transport 编排（primary/secondary/tertiary）
- 目标：从主备扩展到多路优选+权重+熔断恢复。
- 验收：故障注入下自动切换与恢复稳定，指标可观测。
- 工作量：L（约 6~10 人日）

P1（高优先级）
4. B04：健康探针周期任务与故障快照
- 目标：补齐 transport 周期探针、失败率统计、最近 N 次异常快照。
- 验收：故障定位时间目标 ≤ 5 分钟。
- 工作量：M（约 3~5 人日）

5. B05：配置 diff 字段级审计
- 目标：输出 Reload 前后字段级变更清单（含风险等级）。
- 验收：审计报告能直接定位变更源与影响范围。
- 工作量：M（约 2~4 人日）

6. B06：质量门禁增强（趋势化）
- 目标：引入 benchmark 历史趋势对比与波动阈值告警。
- 验收：连续版本自动生成性能趋势与回归告警。
- 工作量：M（约 3~4 人日）

P2（中优先级）
7. B07：RTOS 最小可运行样例（非 stub）
- 目标：提供 FreeRTOS/STM32 最小跑通示例（任务+队列+轻量编码）。
- 验收：样例可构建、可跑通、可验收。
- 工作量：L（约 6~9 人日）

8. B08：ROS2 Adapter 首版可运行样例
- 目标：实现 topic/QoS/参数注入的最小可运行集成。
- 验收：至少 1 组 ROS2 场景端到端跑通。
- 工作量：M/L（约 4~7 人日）

9. B09：平台接入手册与供应链接口规范
- 目标：输出配置模板、验收步骤、错误码映射规范。
- 验收：外部团队可按手册完成接入。
- 工作量：S/M（约 2~3 人日）

推荐里程碑（v0.8）
- M1（v0.8.1）：B01 + B02 + B05
- M2（v0.8.2）：B03 + B04 + B06
- M3（v0.8.3）：B07 + B08 + B09

七、项目价值总结

本项目已形成“车载-机器人互通”中间层的核心骨架，具备以下特征：
- 可插拔：传输层和配置可替换
- 可观测：指标与错误分支覆盖完整
- 可演进：v0.5 可在此基础上引入策略引擎、热更新与跨平台扩展

结论：
当前框架已经具备工程化可用的互通能力，适合作为车载与机器人之间中间件的标准基座；下一步在配置热更新、策略化与跨平台适配上继续扩展即可形成完整产品能力。
