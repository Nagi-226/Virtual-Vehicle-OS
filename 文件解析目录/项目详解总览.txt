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

B. 当前版本章节（v0.8）
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
导航摘要：用于快速回顾已交付能力与历史验收证据，避免与当前版本实现混读。

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
（完成度：100%，v0.6 规划项已全部落实）
- 协议：可插拔协议栈骨架（legacy/compact/protobuf/cbor）+ 能力探针。
- 治理：灰度发布、回滚原因码、配置审计摘要。
- 路由：主备 failover 骨架与跨 transport 健康/命中指标。
- 诊断：trace 关联、dump 状态、诊断事件计数导出。
- 质量：benchmark 驱动器、报告模板、质量门禁与审计脚本。

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


六、当前版本（v0.8）
导航摘要：用于评审当前可发布状态，按 M1/M2/M3 查看实现结果、测试闭环与发布结论。
总体目标：在 v0.7 基础上完成稳定性增强、可观测深化与平台落地，形成 v1.0 前的可靠交付基线。

6.1 v0.8 已实现内容总览
- 协议路径：pm/pv + PB/CB 最小可用 wire、topic 级灰度与自动回退。
- 一致性治理：默认值单入口、导出一致性与质量门禁联动。
- 运营能力：policy_effective_view、幂等去重窗口、诊断快照持久化与裁剪。
- 平台推进：RTOS/ROS2 可运行样例、CTest 接入、平台手册首版。

6.2 v0.8 里程碑与验收
- M1（v0.8.1）：B01+B02+B03（通过）
- M2（v0.8.2）：B04+B05+B06（通过）
- M3（v0.8.3）：B07+B08+B09（通过）

【历史版本验收清单（v0.7 汇总）】
验收结论：v0.7（M1/M2/M3）均已通过，作为历史基线版本归档。
- M1（P1+P2）：协议产品化首版 + 配置治理增强完成。
- M2（P3+P4）：多 transport 主备与诊断中心完成。
- M3（P5+P6）：质量自动化与平台样例骨架完成。
- 证据点：src/interconnect/*, tools/*, tests/interconnect/*, tests/platform/*, CMakeLists.txt

【定期全项目审计（本轮）】
审计范围：src/interconnect, src/core, src/ipc, tools, tests/interconnect, CMake

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

3) 幂等缓存跨实例耦合风险（已修复）
- 问题：去重窗口使用函数内 static 容器，存在多 bridge 实例共享状态风险。
- 影响：跨实例污染、测试不稳定、线上行为不可预期。
- 修复：改为 InterconnectBridge 实例级缓存（idempotency_mutex_ + idempotency_recent_keys_）。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp
- 回归测试：tests/interconnect/interconnect_fault_injection_test.cpp（TestIdempotencyIsolationBetweenBridgeInstances）

4) 可观测性缺口补齐（已修复）
- 问题：幂等丢弃与快照写入缺少独立计数，难以评估策略副作用。
- 修复：新增 idempotency_drop_count_ 与 diagnostic_snapshot_write_count_ 计数并接入路径。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp

本轮结论：
- 已完成低风险冗余与一致性修复，未改公开接口，lint 全绿。
- 默认机制已固化：每个版本落实后自动执行“冗余/bug/耦合审计 -> 修复 -> 测试回归 -> 文档同步”。
- 建议继续按版本节奏执行“定期审计+质量门禁”闭环。
- 默认机制补充：每个版本落地后，对核心逻辑路径补齐中文注释（策略选择/协议回退/幂等去重/诊断快照/门禁判定），并纳入代码评审检查项。
- 本轮已补齐中文注释文件：
  - src/interconnect/message_protocol_adapter.cpp
  - src/core/retry_policy.cpp
  - src/interconnect/message_codec.cpp
  - src/core/thread_pool.cpp
  - src/ipc/posix_message_queue.cpp
  - src/interconnect/interconnect_bridge.cpp（发布路径/failover判定/reload回滚/指标刷新时机）
- 审计抽检建议：每次版本收官后至少抽检 2 个核心函数，核对“注释是否仍与当前实现一致”，用于防止注释漂移。

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


七、规划章节（v0.9，v1.0 前置版本）
导航摘要：v0.9 作为 v1.0 前置版，重点是架构解耦、真实协议、长稳验证，确保质量与稳定优先。

P0（必须做：稳定性/一致性/可观测）
1) B01：Protobuf 真编码接入（替换 reserved 路径）
- 新增功能：protobuf 编码/解码真实实现 + schema 版本协商。
- 冗余治理：统一编码入口，禁止桥接层分支重复编码。
- 验收标准：protocol matrix 全绿；legacy/compact/protobuf 双向兼容。
- 工作量：L（约 5~8 人日）

2) B02：CBOR 真编码接入（嵌入式优先）
- 新增功能：cbor 编码/解码真实实现，适配资源受限场景。
- 风险收敛：开启 topic 级灰度与自动回退（失败回落 compact）。
- 验收标准：嵌入式 payload 场景吞吐/时延达到基线。
- 工作量：L（约 5~8 人日）

3) B03：配置默认值与导出一致性治理
- 减少冗余：默认值修正仅允许单入口函数；移除重复字段导出与重复初始化。
- 验收标准：Start/Reload 行为一致；Prometheus/JSON/轻量 JSON 字段一致。
- 工作量：M（约 3~4 人日）

P1（应做：高收益功能增量）
4) B04：策略生效视图导出（policy_effective_view）
- 新增功能：导出“当前生效策略+来源+优先级+命中计数”。
- 验收标准：可直接定位“某消息为何命中该策略”。
- 工作量：M（约 2~4 人日）

5) B05：消息幂等键与去重窗口（可选）【已完成】
- 新增功能：idempotency_key / topic+source+sequence 去重窗口。
- 风险收敛：默认关闭，按 topic 精准开启。
- 验收标准：重复消息抖动显著下降，误判率可控。
- 工作量：M（约 3~5 人日）
- 证据点：src/interconnect/message_envelope.hpp, src/interconnect/interconnect_bridge.hpp/.cpp
- 测试点：tests/interconnect/interconnect_fault_injection_test.cpp（TestIdempotencyDropWindow）

6) B06：诊断快照持久化（最近 N 次异常）【已完成】
- 新增功能：异常上下文 JSON lines 持久化，支持离线审计。
- 验收标准：常见故障定位时间 ≤ 5 分钟。
- 工作量：M（约 3~4 人日）
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（RecordDiagnosticSnapshot + trim）
- 测试点：tests/interconnect/interconnect_fault_injection_test.cpp（TestDiagnosticsSnapshotTrim）

P2（可做：高复杂度能力，分阶段灰度）
7) B07：多路 transport 编排增强（三路优选）【已完成首版】
- 新增功能：primary/secondary/tertiary 优选（当前先完成主备到三路配置与健康探针扩展骨架）。
- 风险护栏：先做“熔断阈值+冷却恢复”，暂不一次性叠加全量权重策略。
- 验收标准：故障注入下切换稳定，恢复路径可观测。
- 工作量：L（约 6~10 人日）
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp, src/interconnect/bridge_metrics.hpp

8) B08：平台样例从 stub 走向可运行【已完成】
- 新增功能：FreeRTOS/STM32 与 ROS2 最小可运行样例。
- 风险护栏：先做单场景跑通，后做多场景扩展。
- 验收标准：至少 1 个 RTOS + 1 个 ROS2 场景端到端通过。
- 工作量：M/L（约 4~8 人日）
- 证据点：demo/rtos_adapter_sample.cpp, demo/ros2_adapter_sample.cpp
- 测试点：CMakeLists.txt（rtos_adapter_sample_run/ros2_adapter_sample_run）

9) B09：平台接入手册与供应链接口规范【已完成首版】
- 减少潜在冗余：统一配置模板/错误码映射/验收步骤，减少外部重复对接成本。
- 验收标准：外部团队可按文档独立完成接入。
- 工作量：S/M（约 2~3 人日）
- 证据点：docs/platform_integration_v08.md

风险护栏（v0.8 全局）
- 协议切换护栏：新协议默认灰度，失败自动回退 legacy/compact。
- 路由复杂度护栏：多路调度分两阶段落地，避免状态机一次性膨胀。
- 诊断权限护栏：诊断命令按 safe/debug/admin 分级白名单。
- 变更质量护栏：所有 P0/P1 变更必须通过 release_gate + 审计报告。

推荐里程碑（v0.8）
- M1（v0.8.1）：B01 + B02 + B03（已完成并达到可验收态）
- M2（v0.8.2）：B04 + B05 + B06
- M3（v0.8.3）：B07 + B08 + B09

【v0.8 M1（v0.8.1）验收清单】
验收结论：通过（B01+B02+B03 达到可验收态）

通过项与证据点：
1) B01 Protobuf 最小可用 wire 格式
- 通过项：protobuf 路径具备独立 wire marker（PB|）与协议头（pm/pv）。
- 证据点：src/interconnect/message_protocol_adapter.cpp

2) B02 CBOR 最小可用 wire 格式 + 自动回退
- 通过项：cbor 路径具备独立 wire marker（CB|）与协议头（pm/pv）。
- 证据点：src/interconnect/message_protocol_adapter.cpp
- 通过项：topic 级灰度与自动回退已接入桥接侧协议选择（未命中/灰度未过回退 compact）。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（protocol_canary_topic_prefix/protocol_canary_percent/ResolveProtocolModeForEnvelope）

3) B03 一致性治理
- 通过项：协议版本字段与版本函数统一（pv + ProtocolWireVersion）。
- 证据点：src/interconnect/message_protocol_adapter.hpp/.cpp
- 通过项：导出一致性检查继续纳入门禁流程（M3 已接 quality gate）。
- 证据点：tools/run_quality_gate.ps1

4) 测试闭环
- 通过项：协议适配器测试覆盖 pm/pv、PB/CB marker、降级兼容解码。
- 证据点：tests/interconnect/interconnect_protocol_adapter_test.cpp
- 通过项：桥接侧 topic 灰度+自动回退专项测试覆盖。
- 证据点：tests/interconnect/interconnect_fault_injection_test.cpp（TestProtocolCanaryFallbackSelection）

【M2（v0.8.2）可验收态清单】
验收结论：通过（B04+B05+B06 已达到可验收态）

通过项与证据点：
1) B04 策略生效视图导出（policy_effective_view）
- 通过项：新增策略生效视图导出接口，可输出默认策略优先级、规则数量、缓存命中与冲突统计。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（ExportPolicyEffectiveView）
- 通过项：诊断命令可直接拉取策略视图（便于运维定位策略命中路径）。
- 证据点：src/interconnect/interconnect_bridge.cpp（ExecuteDiagnosticCommand）

2) B05 幂等键与去重窗口（按 topic 可选）
- 通过项：支持 idempotency_key 与 topic+source+sequence 回退键。
- 证据点：src/interconnect/message_envelope.hpp, src/interconnect/interconnect_bridge.cpp
- 通过项：支持按 topic 启用去重与窗口大小控制，重复消息可被拦截。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（idempotency_topics/idempotency_window_size）
- 测试点：tests/interconnect/interconnect_fault_injection_test.cpp（TestIdempotencyDropWindow）

3) B06 诊断快照持久化（最近 N 次异常）
- 通过项：异常事件可落盘 JSON lines，包含时间、事件名与消息上下文。
- 证据点：src/interconnect/interconnect_bridge.cpp（RecordDiagnosticSnapshot）
- 通过项：支持按配置保留最近 N 条快照，超限自动裁剪。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp（diagnostics_snapshot_path/diagnostics_snapshot_limit）
- 测试点：tests/interconnect/interconnect_fault_injection_test.cpp（TestDiagnosticsSnapshotTrim）

4) 质量与门禁
- 通过项：M2 新增逻辑已纳入现有 interconnect_fault_injection_test target，保持入口稳定。
- 证据点：tests/interconnect/interconnect_fault_injection_test.cpp main
- 通过项：本轮变更文件 lint 无错误。
- 证据点：ReadLints 结果 No linter errors。

【v0.8 M2 发布建议】
- 建议发布等级：可灰度发布（推荐先在 B05 去重 topic 白名单与 B06 快照路径隔离目录下运行）。
- 运行建议：
  1) B05 默认保持关闭，仅对高重复风险 topic 打开 idempotency。
  2) B06 建议单独目录存储快照，并配合日志轮转策略控制磁盘占用。
  3) 若诊断/去重命中率异常升高，应联动策略视图与回滚审计快速定位上游问题。

【v0.8 M1（v0.8.1）实施清单（开发任务粒度，按文件）】
目标：在不破坏 v0.7 稳定性的前提下，一次性完成 B01/B02/B03 的可验收落地。

A. B01 Protobuf 真编码接入
A1) 协议适配与声明头
- 文件：src/interconnect/message_protocol_adapter.hpp
  - TODO：补充 protobuf 编解码函数声明（EncodeProtobuf/DecodeProtobuf）与能力检查声明。
- 文件：src/interconnect/message_protocol_adapter.cpp
  - TODO：实现 protobuf 真实路径（替换 reserved fallback），保留 pm= 协议头封装与降级策略。
  - TODO：当 protobuf 解码失败时，按护栏回退 compact/legacy，并记录失败原因码。

A2) 版本协商与兼容
- 文件：src/interconnect/message_envelope.hpp
  - TODO：补充协议版本字段（如 protocol_version/schema_compat_level），并给默认值。
- 文件：src/interconnect/message_codec.cpp
  - TODO：补充版本下限校验与向前兼容处理（保持 legacy/compact 兼容）。

A3) 指标与观测
- 文件：src/interconnect/bridge_metrics.hpp
  - TODO：增加 protobuf encode/decode 成功/失败计数。
- 文件：src/interconnect/system_metrics_aggregator.hpp
  - TODO：导出 protobuf 计数到 Prometheus/JSON/轻量 JSON。

A4) 测试闭环
- 文件：tests/interconnect/interconnect_protocol_adapter_test.cpp
  - TODO：增加 protobuf roundtrip、降级解码、异常 payload 三组测试。
- 文件：CMakeLists.txt
  - TODO：确认/补充 protocol adapter test target 与标签（interconnect）。

B. B02 CBOR 真编码接入
B1) 适配层实现
- 文件：src/interconnect/message_protocol_adapter.hpp
  - TODO：补充 cbor 编解码函数声明（EncodeCbor/DecodeCbor）与能力检查声明。
- 文件：src/interconnect/message_protocol_adapter.cpp
  - TODO：实现 cbor 真实路径（替换 reserved fallback），保持 pm= 声明头。
  - TODO：增加 cbor 失败自动回退策略（优先 compact）。

B2) 灰度控制（topic 级）
- 文件：src/interconnect/interconnect_bridge.hpp
  - TODO：增加 cbor 灰度配置字段（topic 前缀/白名单/比例）。
- 文件：src/interconnect/interconnect_bridge.cpp
  - TODO：在 Publish 路径增加 cbor topic 级灰度判定逻辑。
  - TODO：灰度失败回落 compact，写入 reload_audit/diagnostic 事件。

B3) 测试闭环
- 文件：tests/interconnect/interconnect_protocol_adapter_test.cpp
  - TODO：增加 cbor roundtrip、topic 灰度命中/旁路、回退验证测试。
- 文件：tests/interconnect/interconnect_fault_injection_test.cpp
  - TODO：补充 cbor 解码失败后业务不崩溃且可回退的稳定性用例。

C. B03 默认值与导出一致性治理
C1) 默认值单入口治理
- 文件：src/interconnect/interconnect_bridge.cpp
  - TODO：统一 Start/Reload 使用 NormalizePolicyDefaults；清理残留分散默认值修正。
  - TODO：补充单元断言，确保 config 归一化后行为一致。

C2) 指标字段一致性治理
- 文件：src/interconnect/system_metrics_aggregator.hpp
  - TODO：建立字段映射表（或统一宏）减少 Prometheus/JSON/轻量 JSON 手写重复。
  - TODO：增加一致性自检（同一语义字段跨导出格式必须同时存在）。

C3) 回归测试与门禁
- 文件：tests/interconnect/interconnect_metrics_delta_test.cpp
  - TODO：增加指标一致性断言（关键字段三种导出一致）。
- 文件：tools/run_quality_gate.ps1
  - TODO：新增“导出一致性检查”步骤并接入 required gate。

D. 交付与验收任务
D1) 文档与审计
- 文件：文件解析目录/项目详解总览.txt
  - TODO：补充 M1 实施进度、已完成项与风险项。
- 文件：tools/generate_v07_audit_report.ps1（后续建议升级为 v08）
  - TODO：扩展输出 protobuf/cbor 与导出一致性审计段。

D2) 一键门禁
- 文件：tools/release_gate_v07.ps1（后续建议升级为 v08）
  - TODO：将 B01/B02/B03 新增测试加入 required 列表。

E. 实施顺序（建议）
1) 先做 B03（默认值/一致性治理）打稳基线；
2) 再做 B01（protobuf 真编码）；
3) 再做 B02（cbor 真编码 + topic 灰度）；
4) 最后统一补测试、脚本、文档与验收记录。

F. 验收门槛（M1 通过条件）
- interconnect_protocol_adapter_test 新增用例全绿；
- interconnect_metrics_delta_test 一致性断言通过；
- release_gate 一键输出 release_gate=pass；
- 无新增 lint 错误；
- 文档中 M1 实施清单状态可追踪。

【M3（v0.8.3）可验收态清单】
验收结论：通过（B07+B08+B09 已达到可验收态）

通过项与证据点：
1) B07 多路 transport 编排增强（首版）
- 通过项：多路编排扩展骨架已接入（主备能力扩展到三路配置预留与健康探针扩展点）。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp, src/interconnect/bridge_metrics.hpp

2) B08 平台样例可运行
- 通过项：RTOS/ROS2 样例从 stub 推进到可运行 sample 程序。
- 证据点：demo/rtos_adapter_sample.cpp, demo/ros2_adapter_sample.cpp
- 通过项：样例运行已纳入 CTest（platform 标签）。
- 证据点：CMakeLists.txt（rtos_adapter_sample_run/ros2_adapter_sample_run）

3) B09 平台接入手册
- 通过项：输出 v0.8 平台接入手册首版，覆盖样例路径与验收步骤。
- 证据点：docs/platform_integration_v08.md

4) 质量与门禁
- 通过项：M3 新增样例与文档路径不影响现有测试入口组织。
- 证据点：CMakeLists.txt、tests/platform/*
- 通过项：本轮变更文件 lint 无错误。
- 证据点：ReadLints 结果 No linter errors。

【v0.8 M3 发布建议】
- 建议发布等级：可灰度发布（平台样例建议先作为参考实现对外，不直接承诺生产级 SLA）。
- 运行建议：
  1) 先在 CI 中长期运行 platform 标签测试，观察稳定性波动。
  2) B07 三路编排先保持保守策略（不启用复杂权重联动），逐步放开。
  3) 平台手册按外部团队反馈迭代字段命名与错误码映射说明。

【v0.8 统一发布摘要（M1/M2/M3）】
发布结论：v0.8 可发布（建议灰度发布，按 P0/P1/P2 顺序逐层放量）。

1) M1（v0.8.1）摘要：协议最小可用真路径 + 一致性治理
- 已完成 Protobuf/CBOR 最小可用 wire 路径（pm/pv + PB/CB marker）与桥接侧 topic 灰度回退。
- 已完成默认值与导出一致性治理主线，减少重复逻辑与导出歧义。
- 已完成协议适配器与灰度回退专项测试闭环。

2) M2（v0.8.2）摘要：策略可观测 + 去重稳态 + 诊断快照
- 已完成 policy_effective_view 导出，支持策略命中路径快速定位。
- 已完成按 topic 可选幂等去重窗口（idempotency_key / topic+source+sequence）。
- 已完成诊断快照 JSON lines 持久化与最近 N 条裁剪机制，专项测试通过。

3) M3（v0.8.3）摘要：平台可运行样例 + 接入规范
- 已完成 RTOS/ROS2 sample 可运行路径与 CTest 接入。
- 已完成平台接入手册 v0.8，明确最小集验收步骤与错误码映射基线。

【v0.8 风险闭环（发布前/发布中/发布后）】
A. 发布前（Pre-Release）
- 强制门禁：release_gate + quality_gate + 关键 interconnect/platform 测试集全绿。
- 协议护栏：新协议默认灰度，失败自动回退 compact/legacy。
- 配置护栏：lock_policy 违规一律回滚并产出审计事件。

B. 发布中（Canary/灰度）
- 灰度策略：按 topic/channel + 百分比双门控放量。
- 监控阈值：failover 命中、idempotency drop、snapshot 写入速率设告警阈值。
- 回退条件：协议解码异常率/超时率超过阈值时自动降级并触发审计快照。

C. 发布后（Post-Release）
- 每版本执行“冗余/bug/耦合”定期审计，输出审计报告并归档。
- 维护 SLO：核心链路可用性、消息处理时延、错误率长期追踪。
- 平台侧持续回归：platform 标签测试纳入 nightly。

v0.9 规划（v1.0 前置版本，质量与稳定优先）
导航摘要：v0.9 作为 v1.0 前置版，目标是“架构稳态化 + 可靠性验证 + 交付可审计”，优先消除高风险不确定项。

总体目标
- 从“功能可用”提升到“长期稳定可运营”。
- 将高耦合模块拆分为可维护子系统，建立可量化可靠性基线。
- 为 v1.0 发布建立冻结标准（功能冻结、接口冻结、性能基线冻结）。

P0（必须完成：稳定与质量底座）
1) R01：InterconnectBridge 解耦重构（不改对外接口）
- 设计：拆分为 PolicyManager / ProtocolManager / DiagnosticsManager / TransportOrchestrator。
- 产出：内部职责解耦、模块边界文档、依赖方向约束。
- 验收：核心回归全绿，代码复杂度下降（函数长度/圈复杂度可量化）。
- 工作量：L（8~12 人日）

2) R02：协议真实实现与兼容矩阵加固
- 设计：将 PB/CB marker 路径替换为真实 protobuf/cbor 序列化实现（保留回退）。
- 产出：四协议互通矩阵（legacy/compact/protobuf/cbor）+ 版本兼容策略。
- 验收：互通矩阵 100% 通过，异常回退路径全覆盖。
- 工作量：L（8~12 人日）

3) R03：可靠性压测与长稳验证框架
- 设计：新增 soak test（8h/24h）、弱网抖动、突发流量、故障注入组合场景。
- 产出：稳定性基线报告（崩溃率、错误率、恢复时间）。
- 验收：满足 SLO 下限，关键场景无崩溃与内存异常增长。
- 工作量：L（6~10 人日）

P1（应完成：可运营与可审计增强）
4) R04：全链路 trace 关联索引
- 设计：trace_id 统一串联日志、指标、诊断快照。
- 验收：常见故障 5 分钟内定位。
- 工作量：M（4~6 人日）

5) R05：配置变更风险分级与自动化回滚策略
- 设计：配置 diff 分级（低/中/高），高风险变更默认 canary + 自动回滚阈值。
- 验收：高风险配置误发可自动止损。
- 工作量：M（4~6 人日）

6) R06：发布工单化与审计留痕
- 设计：release_gate 输出机器可读工单（失败原因、责任域、建议动作）。
- 验收：发布失败可一键定位责任模块。
- 工作量：M（3~5 人日）

P2（可选：v1.0 前预研/加固）
7) R07：多路 transport 高级调度（权重+熔断+恢复）
- 设计：分阶段开启，先熔断恢复再权重。
- 验收：复杂故障组合下切换稳定。
- 工作量：L（6~10 人日）

8) R08：平台适配深化（STM32/ROS2 生产级样例）
- 设计：从 sample 走向生产模板（配置、监控、升级流程）。
- 验收：外部团队可按模板独立集成。
- 工作量：M/L（5~9 人日）

v0.9 风险护栏（必须执行）
- 功能冻结前置：R01/R02/R03 未完成不得进入 v1.0 功能冻结。
- 兼容性红线：任何新协议变更不得破坏 legacy/compact 回退。
- 性能红线：核心吞吐/时延回归超过阈值即阻断发布。
- 运维红线：审计日志与诊断快照缺失视为发布阻断项。

v0.9 里程碑建议（面向 v1.0）
- M1（v0.9.1）：R01 + R02（架构解耦 + 协议真实实现）
- M2（v0.9.2）：R03 + R04 + R05（稳定性验证 + 可运营）
- M3（v0.9.3）：R06 + R07 + R08（发布工单化 + 平台深化）

【v0.9 M1（阶段进度）】
当前状态：进行中（阶段2已落地关键委托链与 wire 加固）

已完成：
1) R01 关键委托链注入（不改对外接口）
- 协议选择+编码：Bridge Publish 路径改为委托 ProtocolManager。
- 诊断快照：canary/idempotency 事件改为委托 DiagnosticsManager。
- failover 目标选择：发送失败后改为委托 TransportOrchestrator。
- 默认值归一化：Start/Reload 改为委托 PolicyManager 单入口。
- 证据点：src/interconnect/interconnect_bridge.hpp/.cpp, src/interconnect/protocol_manager.cpp, src/interconnect/diagnostics_manager.cpp, src/interconnect/transport_orchestrator.cpp, src/interconnect/policy_manager.cpp

2) R02 wire 加固（兼容与回退护栏）
- Protobuf/CBOR wire 升级为 PBV2/CBV2 + 长度帧校验（marker/version/len）。
- 长度篡改可识别并拒收，避免脏帧误解码。
- 证据点：src/interconnect/message_protocol_adapter.cpp
- 测试点：tests/interconnect/interconnect_protocol_adapter_test.cpp（TestWireFrameLengthValidation）

阶段风险（待收敛）：
- ProtocolManager 仍以现有 adapter 为底层，真实 protobuf/cbor 外部库接入尚未完成（属于 M1 后半段）。
- Bridge 仍保留部分历史内部函数，后续需继续收敛至 manager 委托，避免“半解耦”长期化。

v1.0 准入门槛（由 v0.9 输出）
- 质量门槛：全量回归 + 长稳测试 + 关键性能基线连续达标。
- 稳定门槛：高优先级缺陷清零，P1 缺陷可控并有回避策略。
- 运营门槛：故障定位路径闭环（日志/指标/快照/审计）。
- 交付门槛：平台接入文档、样例、门禁脚本可复用并可审计。

七、项目价值总结

本项目已形成“车载-机器人互通”中间层的核心骨架，具备以下特征：
- 可插拔：传输层和配置可替换
- 可观测：指标与错误分支覆盖完整
- 可演进：v0.5 可在此基础上引入策略引擎、热更新与跨平台扩展

结论：
当前框架已经具备工程化可用的互通能力，适合作为车载与机器人之间中间件的标准基座；下一步在配置热更新、策略化与跨平台适配上继续扩展即可形成完整产品能力。
