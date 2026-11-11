# SCS0009 标定（数据结构 + 实机流程）

状态：schema/loader/示例已就绪；真实值待硬件（#24）。示例 data/calibration/servo_map.example.json 不是实测结果。

## 1. 文件格式

data/calibration/servo_map.json：

~~~json
{
  "version": 1,
  "servos": [
    {
      "id": 1,
      "middle_raw": 2048,
      "direction": 1,
      "min_raw": 700,
      "max_raw": 3400,
      "min_deg": -90.0,
      "max_deg": 90.0,
      "max_speed": 100,
      "torque_limit": 500,
      "load_threshold": 384
    }
  ]
}
~~~

- 8 个 id 必须是 1..8，每个一次；
- direction = +1/-1；
- min_raw < max_raw；middle_raw 在范围内；
- min_deg < max_deg；
- load_threshold 是接触判定 raw 阈值，需实机调；
- version 用于以后兼容。

## 2. 代码

- host/app/control/calibration.py：load_calibration、deg_to_raw、raw_to_deg；
- host/tests/test_calibration.py：示例文件与换算测试；
- 示例文件：data/calibration/servo_map.example.json。

## 3. 实机流程（待硬件）

1. 单舵机、独立 5V、共地、保险丝；
2. 烧 passthrough 固件（AH_CONTROL_APP=0）；
3. python tools/probe_servos.py --port /dev/serial0 --via mcu；
4. 逐个确认 id、方向、中位、限位；
5. 小幅移动，确认不撞机械限位；
6. 填 servo_map.json，重新跑 controller fake/串口测试；
7. 记录原始端口/命令/输出/照片/波形到 docs/evidence/ 或当日日志。

## 4. 验收

- [ ] 8 路 id/方向/中位/限位完整
- [ ] deg↔raw 往返测试通过
- [ ] 标定文件能驱动 controller/backend，不再硬编码 middle
- [ ] 真实值有原始证据
- [ ] 未完成项明确标记，不用示例值冒充实测

## 5. 安全

- 标定前先断电检查接线；
- 第一次上电使用低速度/小角度；
- 不把舵机 5V 接到 Pi/STM32；
- 急停/断电开关在手边。
