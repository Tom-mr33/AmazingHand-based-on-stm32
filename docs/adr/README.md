# Architecture Decision Records

本目录记录跨模块契约与不可轻易回退的决策。当前范围：Pi 总控 + STM32F103 设备层，无视觉、无 Simulink。

| ADR | 标题 | 状态 |
|---|---|---|
| 0001 | STM32 控制协议 v2 | accepted |
| 0002 | Pi 总控 / STM32 设备层边界 | accepted |
| 0003 | 安全模型：停止、看门狗与单一输出所有者 | accepted |

修改协议、职责边界或安全行为时，先更新对应 ADR，再改代码。
