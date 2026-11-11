# 树莓派部署（Pi 总控 + STM32F103 设备层）

状态：文档准备完成；真实硬件待接。当前不接相机/麦克风，不安装 MuJoCo。

## 1. 系统

- Raspberry Pi OS 64-bit Bookworm，Python 3.11；
- 只安装控制依赖（pyserial 等）：pip install -r requirements-pi.txt；
- 不安装视觉/语音/MuJoCo 依赖。

## 2. UART

1. sudo raspi-config
   - Interface Options -> Serial -> 启用硬件串口；
   - 关闭 serial console 登录。
2. 确认 /dev/serial0 存在，当前用户属于 dialout 组。
3. 若使用板载 Bluetooth 占用 PL011，按需在 /boot/firmware/config.txt 加：
   - enable_uart=1
   - dtoverlay=disable-bt
4. 重启后用 ls -l /dev/serial0 与 stty -F /dev/serial0 115200 检查。

## 3. 接线

| 信号 | Pi | STM32F103 |
|---|---|---|
| Pi TX | GPIO14 (TXD) | PA3 (USART2_RX) |
| Pi RX | GPIO15 (RXD) | PA2 (USART2_TX) |
| GND | GND | GND（与舵机电源共地） |
| SCS DATA | — | PA9（1 Mbps 单线半双工） |

- 舵机 5V 必须独立供电，禁止从 Pi/STM32/USB 取电；
- 保险丝、共地、上电顺序按 firmware/stm32/README.md；
- 当前没有急停按钮；真机调试前应加物理断电/急停。

## 4. 固件

~~~sh
cd firmware/stm32
cmake --preset Debug
cmake --build --preset Debug
# ST-Link 烧录 build/Debug/ah_passthrough.elf
~~~

- AH_CONTROL_APP=1：control 模式（协议 v2）；
- AH_CONTROL_APP=0：passthrough，用于单舵机扫描/标定；
- 烧录后先不接舵机，确认 3.3V/5V/共地。

## 5. 首次 bring-up

1. passthrough 固件 + 单舵机：
   - python tools/probe_servos.py --port /dev/serial0 --via mcu
   - python tools/blink_servo.py --port /dev/serial0 --via mcu --id 1
2. 记录 8 个 ID/方向/中位/限位，填写 data/calibration/servo_map.json；
3. 切 control 固件，跑：
   - PYTHONPATH=host python -m app.controller.cli --transport serial --port /dev/serial0
4. 先 open/status，再 grasp/hold/release，再 STOP/RESET。

## 6. 无硬件冒烟（在 Pi 上也能跑）

~~~sh
python tools/run_smoke.py
PYTHONPATH=host python -m app.controller.cli --transport fake --script "grasp,release,status"
~~~

## 7. 验收清单

- [ ] /dev/serial0 可用，dialout 权限正确
- [ ] fake CLI 在 Pi 上通过
- [ ] passthrough 单舵机/8 路扫描
- [ ] control 固件 HELLO/ACCEPT、ACTUAL/HEALTH
- [ ] STOP 后 torque off，RESET 不恢复旧目标
- [ ] 拔 Pi/断连后 STM32 500 ms 内安全停止
- [ ] 真实标定文件 8 路完整
- [ ] 不使用相机/麦克风/MuJoCo

## 8. 已知边界

- Linux 非实时；抓取实时执行在 STM32，Pi 只做业务/CLI；
- 真实 UART 1 Mbps SCS 时序/电平未验证；
- 没有硬件急停，真机必须外接断电开关。
