# README 重排规划（原 README 第 3–222 行）

> 这份文件原先是 `README.md` 的开头部分，2026-11-13 从 README 拆出。
> 它不是「项目介绍」，而是一份**文档结构改版规划**——记录当时打算怎么把文档重排成
> 「历史版本 → 当前版本 → 下一版本规划」三段式。改版已完成/已作废后，它仍有作为**规划记录**的价值。
>
> **现状**：README 已重写为面向访客的项目介绍（见仓库根 `README.md`）；
> 历史版本与审计内容移至 [`version-history.md`](./version-history.md)。

---

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
- **热更新支持的字段范围（重要，原为隐藏行为）**：`ReloadConfigIfChanged` 只支持修改
  **非策略字段**（如 `receive_priority`、采样率、诊断上报间隔、canary 开关等）；
  一旦候选配置改动了 **策略字段**（`policy_table` / `sla_policy`），reload 会拒绝并回滚，
  诊断字段为 `reload_rollback_reason_code = 7`、审计摘要
  `reason=policy_fields_unsupported`，并在日志中提示"需重启生效"。
  这类改动**必须重启进程**，不能靠热更新落地。
  注意区分：`reload_rollback_reason_code = 4`（`signature_mismatch`）才是真正的签名不符
  （例如配置文件被外部改动/损坏），两者排查路径不同。
- **已知限制（Known Limitations）· 配置灰度覆盖范围（P1 功能空白）**：
  config-canary 灰度门禁当前**仅作用于 Start()**，**不覆盖 `ReloadConfigIfChanged()`**。
  也就是说：通过**热更新**下发的新配置**不受灰度保护，会直接全量生效**（策略字段除外，
  会被拒绝回滚，见上一条）。
  这与 canary 的设计意图相悖——灰度本意是"新配置先给小流量试"，而运维改配置走的恰恰是
  reload，**灰度实际只保护了冷启动这条最不常发生的路径**。
  ⚠️ 不要把"已启用 enable_config_canary"理解为"所有配置变更都受灰度保护"。
  待决策的两个语义选项与各自后果见 `docs/backlog.md` 的 **BL-01**。
  代码位置标记：`InterconnectBridge::ReloadConfigIfChanged()` 函数体开头有显式注释。
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
