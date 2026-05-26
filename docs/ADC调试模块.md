# ADC 调试模块

## 概述

SA 开关触发的 ADC 调试工具，用于诊断摇杆四通道（Roll/Pitch/Throttle/Yaw）的 ADC 值与滤波输出。

双宏门控设计：只有 **同时定义** `ADC_DEBUG` 和 `ADC_DEBUG_INTERVAL_MS` 时才编译，否则零开销。

## 启用

在 `ESP32/main/CMakeLists.txt` 或组件 `CMakeLists.txt` 中添加：

```cmake
add_compile_definitions(ADC_DEBUG ADC_DEBUG_INTERVAL_MS=1000)
```

| 宏 | 说明 | 默认值 | 范围 |
|---|---|---|---|
| `ADC_DEBUG` | 启用调试模块 | - | 定义了才编译 |
| `ADC_DEBUG_INTERVAL_MS` | 打印间隔 (ms) | - | [10, 10000]，超出自动钳位 |

## 操作方式

| 操作 | 行为 |
|---|---|
| 按一次 SA | 进入调试模式，开始按间隔打印四通道 raw→mapped 值 |
| 再按一次 SA | 退出调试模式，打印统计摘要 |

## 输出格式

### 运行时日志

```
ADC_DBG: #0 R:r1523→1500 P:r1488→1500 T:r1045→1000 Y:r1510→1500
ADC_DBG: #1 R:r1521→1500 P:r1490→1500 T:r1042→1000 Y:r1508→1500
...
```

每行格式：`#序号 R:r<raw>→<mapped> P:r<raw>→<mapped> T:r<raw>→<mapped> Y:r<raw>→<mapped>`

- `raw` = ADC 直读后映射到 1000-2000 的值（经过死区/迟滞之前）
- `mapped` = 经过完整滤波管线后的 src[0..3] 输出值

### 退出统计

```
ADC_DBG: [Roll]  n=300 mean=1501 med=1500 min=1497 max=1504 p10=1499 p90=1503 Δ=7
ADC_DBG: [Pitch] n=300 mean=1502 med=1500 min=1496 max=1505 p10=1498 p90=1504 Δ=9
ADC_DBG: [Thr]   n=300 mean=1001 med=1000 min=1000 max=1005 p10=1000 p90=1002 Δ=5
ADC_DBG: [Yaw]   n=300 mean=1500 med=1500 min=1496 max=1503 p10=1498 p90=1502 Δ=7
```

| 字段 | 含义 |
|---|---|
| n | 采样数（最大 300） |
| mean | 算术平均 |
| med | 中位数 |
| min/max | 最小/最大值 |
| p10/p90 | 第 10/90 百分位 |
| Δ | 峰峰值 (max - min) |

## 采样缓冲

每个通道最多保存 300 个样本（`ADC_DEBUG_MAX_SAMPLES`）。超出的样本不存储但日志继续打印。

## 调试建议

1. **摇杆回中测试** — 进入调试模式，手离开摇杆，观察 mean 是否接近 1500，Δ 是否在死区范围内（≤3）
2. **油门行程测试** — 进入调试模式，慢推油门到底再回中，检查 min 是否为 1000、max 是否为 2000
3. **摇杆全行程测试** — 画圈后退出，检查 p10 和 p90 是否在预期行程范围内

## 禁用

注释或移除 CMakeLists.txt 中的 `add_compile_definitions` 行，重新编译即可。所有调试代码和静态变量均不会被编译。
