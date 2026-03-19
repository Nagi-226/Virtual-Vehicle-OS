Virtual-Vehicle-OS 项目详解总览

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


五、可扩展方向（v0.5规划方向）

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

v0.6 规划方向（新增）
（当前完成度：约 92%，核心能力已闭环，剩余为 protobuf/cbor 实编码与深度健康探针）
1. 可插拔消息协议栈
- 引入 Protobuf/CBOR 编码适配层，支持运行时切换与回退。
- 提供协议能力探针与端到端兼容性验证。
- 任务拆解：
  - T1.1 增加 BridgeConfig.protocol_mode 与运行时切换入口（已完成）。
  - T1.2 增加 MessageCodec compact/legacy 双编码路径与兼容解码（已完成）。
  - T1.3 预留 protobuf/cbor 适配接口与能力探针指标（已完成能力探针指标导出）。

2. 配置发布治理
- 支持灰度发布策略（按域/按比例/按时段）与回滚原因记录。
- 引入配置变更审计日志与版本对比输出。
- 任务拆解：
  - T2.1 增加 canary 字段与按比例灰度门控（已完成）。
  - T2.2 增加配置变更审计摘要（source/version/signature diff）（已完成）。
  - T2.3 增加回滚原因码与状态导出（已完成结构化导出）。

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

六、平台适配路线（FreeRTOS / STM32 / ROS2）

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


七、项目价值总结

本项目已形成“车载-机器人互通”中间层的核心骨架，具备以下特征：
- 可插拔：传输层和配置可替换
- 可观测：指标与错误分支覆盖完整
- 可演进：v0.5 可在此基础上引入策略引擎、热更新与跨平台扩展

结论：
当前框架已经具备工程化可用的互通能力，适合作为车载与机器人之间中间件的标准基座；下一步在配置热更新、策略化与跨平台适配上继续扩展即可形成完整产品能力。
