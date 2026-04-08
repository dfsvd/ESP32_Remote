# FPV RC

这是一个基于 ESP-IDF 的 FPV 遥控器固件工程，当前包含模拟量采集、USB HID 输出、Wi‑Fi 配网页面，以及给外置高频头使用的 CRSF 发射功能。

## 当前主要功能

- ADC 读取摇杆、旋钮和开关输入
- USB 模式输出遥控器数据
- Wi‑Fi AP + WebSocket 网页调参
- CRSF 通道持续发送
- 网页控制 CRSF Bind 和功率
- NVS 保存校准参数、模式和 CRSF 功率

## 蓝牙模拟器支持

当前 BLE 模拟器模式已经切换到 **Apache NimBLE + HID over GATT**，用于无线连接电脑玩模拟器。

说明：

- 蓝牙当前只做 **BLE HID**
- 底层协议栈已从 Bluedroid 切换为 **NimBLE**
- 不包含蓝牙 Xbox 兼容
- USB 仍然保留原有 HID / Xbox 两种模式
- 蓝牙会复用当前 USB 使用的摇杆和开关语义
- `GPIO_NUM_18 == 0` 时进入蓝牙模式
- 蓝牙模式下设备会以 `FPV RC BLE V2` 名称广播
- 广播包只携带 HID 16-bit UUID、设备名和外观信息，避免超长广播
- 主机完成连接、订阅通知并建立加密/绑定后，才会开始发送 HID 报文
- 蓝牙断开后会自动重新进入广播，等待再次连接
- BLE 输入数据通过单元素队列转发，避免蓝牙任务直接并发读取共享 `joy` 结构

相关文件：

- `components/RC/include/rc_ble.h`
- `components/RC/src/rc_ble.c`

使用方式：

1. 上电时让 `GPIO_NUM_18 == 0`，进入 BLE 模式。
2. 查看串口日志，确认已经进入 NimBLE HID 初始化并开始广播。
3. 在电脑或手机蓝牙列表中搜索 `FPV RC BLE V2`。
4. 完成配对并连接。
5. 主机启用 HID 输入通知后，系统会识别为一个 HID 游戏控制器。
6. 进入模拟器或系统游戏控制器测试页面检查轴和按钮。

注意：

- `GPIO_NUM_17 == 0` 走 Wi‑Fi 配网页面模式。
- 默认分支走 USB 模式。
- 蓝牙下目前目标是标准模拟器兼容，不是 Xbox 协议兼容。
- HID 报文保持 4 轴 + 8 按钮，轴逻辑范围保持 `1000..2000`，不会回退到 0 中位映射。

## CRSF 接线模式

项目现在支持两种 CRSF 物理接线方式，切换入口在：

- `components/RC/include/rc_crsf.h`

### 1. 双线 UART 模式（默认）

适用于 TX / RX 分开的设备。

需要关注这些宏：

```c
#define CRSF_LINK_MODE               CRSF_LINK_MODE_UART_FULL_DUPLEX
#define CRSF_UART_TX_PIN             GPIO_NUM_21
#define CRSF_UART_RX_PIN             GPIO_NUM_20
```

接线方式：

- 开发板 `CRSF_UART_TX_PIN` -> 高频头 CRSF RX
- 开发板 `CRSF_UART_RX_PIN` -> 高频头 CRSF TX
- GND -> GND

### 2. 单线半双工模式

适用于只有一根 CRSF 信号线的外置高频头。

把模式改成：

```c
#define CRSF_LINK_MODE               CRSF_LINK_MODE_UART_HALF_DUPLEX
#define CRSF_UART_HALF_DUPLEX_PIN    GPIO_NUM_21
```

接线方式：

- 开发板 `CRSF_UART_HALF_DUPLEX_PIN` -> 高频头 CRSF
- GND -> GND

说明：

- 单线模式下，TX 和 RX 复用同一个 GPIO。
- 固件内部会把 UART 配置成 half-duplex。
- 是否能稳定工作，还取决于你的外置高频头电气接口是否兼容这种单线 CRSF 接法。

## 网页功能

当前网页不需要修改，仍然保留这些能力：

- 查看实时通道
- 保存校准
- 设置 CRSF 功率
- 发送 Bind 指令

相关页面文件：

- `components/RC/src/index.html`

## 关键代码位置

- CRSF 头文件与接线切换：`components/RC/include/rc_crsf.h`
- CRSF 协议与 UART 发送：`components/RC/src/rc_crsf.c`
- Wi‑Fi / WebSocket / NVS：`components/RC/src/rc_wf.c`
- 程序启动入口：`main/main.c`

## 使用说明

1. 在 `components/RC/include/rc_crsf.h` 里选择 CRSF 接线模式。
2. 按所选模式完成硬件接线。
3. 编译并烧录固件。
4. 上电后查看串口日志，确认 CRSF 启动日志显示的模式和 GPIO 是否正确。
5. 连接设备热点，打开网页进行校准、功率设置和 Bind。

## 备注

- 当前 CRSF 已实现发送链路，并预留了后续继续扩展的空间。
- 当前网页协议和页面布局不需要因单线/双线切换而修改。
- 如果你的高频头只有单线 CRSF，但 Bind / 功率命令不生效，需要继续根据目标模块协议细节调整命令帧。
