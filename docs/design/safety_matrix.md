# 控制链路安全矩阵（host 冒烟 / 硬件待验）

状态：host 冒烟已覆盖部分路径；真实电气/力控/急停未验证（#31）。不要用本表把仿真结果写成实机验收。

| 场景 | host 冒烟 | 真实硬件 |
|---|---|---|
| STOP 断力矩 + 锁存 | controller/test_stop_barrier_and_reset；device STOP 测试 | 待验 |
| RESET 不恢复旧目标 | controller reset 回归；device reset 测试 | 待验 |
| 目标看门狗 | controller target_timeout；device target watchdog | 待验 |
| 链路心跳超时 | controller link_timeout；device link watchdog | 待验 |
| 单路离线 | device offline 测试 | 待验 |
| 过温 | device overtemp 测试 | 待验 |
| 过载/堵转 | device overload 测试 | 待验 |
| CRC/seq/旧 session | protocol v2 测试；standin 拒绝测试 | 待验 |
| STOP 屏障（等 actual 确认） | controller stop barrier 测试 | 待验 |
| Pi 崩溃/断连 | fake/standin 断连 | 待验 |
| 真实 1 Mbps/电平/共地 | 不覆盖 | 待验 |
| 真实急停/断电 | 不覆盖 | 待验 |

## 证据要求

- host：测试命令、输出、失败断言；
- 硬件：串口日志、telemetry、波形/照片、接线与电源记录；
- 未测/不可测项明确写“未验证”，不能用 fake 结果替代。

## 命令

~~~sh
python tools/run_smoke.py
PYTHONPATH=host python -m app.controller.cli --transport fake --script "grasp,stop,reset,status"
~~~
