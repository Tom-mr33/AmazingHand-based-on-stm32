# STM32 控制协议 v2（实现说明）

对应 ADR：docs/adr/0001-stm32-control-protocol-v2.md

## 帧

~~~
AA 55 | VER=2 | TYPE | SEQ u16 LE | LEN u8 | PAYLOAD | CRC16-LE
~~~

- 最大 payload 96 B；最大帧 105 B；
- CRC16-CCITT（poly 0x1021，init 0xFFFF），低字节在前；
- parser 拒绝错误版本、超长、坏 CRC，并支持分片/前导垃圾。

## 消息与 payload

| 类型 | 方向 | 大小 | 关键字段 |
|---|---|---|---|
| HELLO 0x01 | Pi→STM32 | 10 | proto、node、session、profile、caps |
| ACCEPT 0x02 | STM32→Pi | 11 | proto、status、session、fw、servo_count、caps |
| HEARTBEAT 0x03 | 双向 | 8 | session、uptime |
| EXECUTION 0x04 | Pi→STM32 | 83 | mode、target[8]、close[8]、load_threshold[8]、speed[8]、torque_limit[8]、timeout |
| ACTUAL 0x05 | STM32→Pi | 55 | raw[8]、load[8]、temp[8]、online_mask、sample_time、age、error[8] |
| HEALTH 0x06 | STM32→Pi | 14 | phase、stop_reason、fault、watchdog、flags、counters[4] |
| OPERATION 0x07 | 双向 | 6+N | op_id、op_type、status、data |
| INFO 0x08 | STM32→Pi | 10 | proto、fw、servo_count、caps、build |

## 实现

- Python：host/app/control/stm32_protocol_v2.py
- C：firmware/stm32/Core/{Inc,Src}/ah_protocol_v2.*
- 固件 app：firmware/stm32/Core/{Inc,Src}/ah_app.*
- Golden vectors：host/tests/test_stm32_protocol_v2.py 与 firmware/stm32/tests/test_ah_protocol_v2.c 互验
- 冒烟：tools/run_smoke.py
