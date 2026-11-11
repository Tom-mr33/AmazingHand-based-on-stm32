# ADR-0003：安全模型

状态：accepted  
日期：2026-11-11

## 决策

- 执行器输出只有一个所有者；安全层只发 stop(reason)，不直接写驱动。
- stop_reason 分开：NONE / USER_STOP / LINK_TIMEOUT / TARGET_TIMEOUT / ESTOP / FAULT / SESSION_RESET。
- 急停本地锁存，只能本地清除；RESET 不自动恢复旧目标。
- 目标看门狗只由成功应用的新 EXECUTION 刷新；HEARTBEAT 不刷新。
- 链路心跳与目标鲜度分开。
- 缺 actual/online 不补零；故障/离线进入安全停止。
- 不静默钳位：越界在 execution 接纳层拒绝，驱动只执行已验证值。
- STOP 后必须等 ACTUAL/HEALTH 确认 stopped/torque off，才允许新 Motion。
- 硬件安全验收（真实电平、1 Mbps、堵转/过温、急停）不能用 host 冒烟替代。

## 后果

- host 冒烟可覆盖状态/协议/故障路径；
- 真机安全项必须在 #24/#31 中单独验收并保留原始证据。
