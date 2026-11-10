# ADR-0002：Pi 总控 / STM32 设备层边界

状态：accepted  
日期：2026-11-11

## 背景

如果业务状态机、执行目标和安全保护分散在 Pi 与 STM32，无法回答“当前谁决定动作、谁拥有最终否决”。同时手部抓取需要 2 ms 级 SCS 总线和本地安全执行，Pi 不适合承担硬实时。

## 决策

- **Pi 是唯一业务状态所有者**：IDLE / GRASPING / HOLDING / RELEASING / FAULT / ESTOP；持有 Motion、execution、actual、session、全停屏障。
- **STM32 只做设备层**：SCS 调度、grasp executor、目标看门狗、急停/故障保护、final veto；不跑业务 FSM。
- 抓取的实时执行（预张/闭合/接触/保持/释放）是设备级 operation，不是业务状态。
- 无视觉、无 Simulink、无模型参考；当前 Pi 用 CLI 手动命令。
- Pi 侧内存不设限；STM32 保持静态分配、无动态内存。

## 后果

- 业务状态变化必须由 Pi 根据 ACTUAL/HEALTH 推进；
- STM32 可以在 Pi 断连/故障时独立进入安全停止；
- 新增抓取策略不应写进 STM32 业务代码，只通过 execution 参数/operation 表达。
