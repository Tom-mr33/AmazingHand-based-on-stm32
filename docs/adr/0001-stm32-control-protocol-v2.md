# ADR-0001：STM32 控制协议 v2

状态：accepted  
日期：2026-11-11

## 背景

v1 帧（AA55 | VER=1 | TYPE | SEQ u8 | LEN | PAYLOAD | CRC16）能跑基础命令，但缺少：

- session / 节点类型 / profile revision；
- execution / actual / health 的明确角色；
- operation_id 的重放/拒绝语义；
- target watchdog 与 link heartbeat 的区分；
- 缺样本语义；
- 共享 golden vectors。

## 决策

采用 v2 帧：

~~~
AA 55 | VER=2 | TYPE | SEQ u16 LE | LEN u8 | PAYLOAD | CRC16-LE
~~~

- 最大 payload 96 B，最大帧 105 B；
- CRC16-CCITT，poly 0x1021，init 0xFFFF；
- CRC 只证明承载完整，版本/session/范围/角色在 codec 之后校验；
- 消息：HELLO / ACCEPT / HEARTBEAT / EXECUTION / ACTUAL / HEALTH / OPERATION / INFO；
- EXECUTION 携带完整 grasp plan：mode、target[8]、close[8]、load_threshold[8]、speed[8]、torque_limit[8]、target_timeout_ms；
- HEALTH 携带 phase、stop_reason、fault_bits、watchdog_state、flags、counters；
- session 变化清旧目标/旧 actual/旧 operation 关联；
- operation_id 用于 RESET / CALIBRATE / APPLY_CONFIG，单槽结果；
- Python 与 C 共享 golden vectors。

## 后果

- Pi 与 STM32 的协议实现必须同步升级；
- v1 保留用于 bring-up/旧测试，不作为正式控制路径；
- 协议变更必须先更新本 ADR。
