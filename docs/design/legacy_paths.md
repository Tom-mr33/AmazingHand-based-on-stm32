# 新旧控制路径对照（2026-11-11）

正式控制链路是 **Pi controller + 协议 v2 + STM32 设备层**。仓库仍保留一些旧路径，用于 bring-up、历史测试和单舵机调试；新增代码不要依赖旧路径。

| 路径 | 状态 | 用途 | 正式替代 |
|---|---|---|---|
| host/app/main.py | LEGACY 默认 | 旧视觉/mock 主状态机、历史冒烟 | `--controller` 或 host/app/controller/cli.py |
| host/app/control/backend_stm32.py | LEGACY v1 | 旧 FSM/遥测后端 | controller + serial_transport.py |
| host/app/control/stm32_protocol.py | LEGACY v1 | 旧协议编解码 | stm32_protocol_v2.py |
| host/app/control/backend_mcu.py | bring-up | 原始 SCS 透传（115200） | control 固件 + serial_transport |
| host/app/control/backend_serial.py | bring-up | USB-TTL 直连 SCS | 可保留用于单舵机/标定 |
| host/app/control/scs_protocol.py | 保留 | SCS golden model | 不变 |
| sim/ | 可选 | PC MuJoCo 演示 | 不进入控制路径 |

## 原则

- 新功能只写 v2 controller / v2 protocol / device 层。
- 旧路径的测试继续跑，防止回归；不为旧路径增加新功能。
- 视觉/语音暂缓；main.py 的 mock 视觉不作为当前验收。
- bring-up 路径（mcu/serial）与正式路径不混用同一会话。
