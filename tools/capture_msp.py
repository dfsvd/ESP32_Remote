#!/usr/bin/env python3
"""
捕获飞控 MSP 通信全流程
记录 Betaflight Configurator 连接时发送/接收的所有数据
"""
import serial
import time
import sys
import os
from datetime import datetime

PORT = '/dev/ttyACM0'
BAUD = 115200
OUTPUT = f'msp_capture_{datetime.now().strftime("%H%M%S")}.log'

# MSP 命令名称
MSP_CMDS = {
    1: 'MSP_API_VERSION', 2: 'MSP_FC_VARIANT', 3: 'MSP_FC_VERSION',
    4: 'MSP_BOARD_INFO', 5: 'MSP_BUILD_INFO', 10: 'MSP_NAME',
    12: 'MSP_STATUS', 13: 'MSP_RAW_IMU', 14: 'MSP_SERVO',
    15: 'MSP_MOTOR', 16: 'MSP_RC', 20: 'MSP_DEBUG',
    21: 'MSP_DEBUG2', 22: 'MSP_DEBUG3',
    100: 'MSP_STATUS_EX', 101: 'MSP_UID', 102: 'MSP_GPS_SV_INFO',
    103: 'MSP_MIXER', 104: 'MSP_BOXNAMES', 105: 'MSP_PIDNAMES',
    106: 'MSP_WP', 108: 'MSP_BAT_CONFIG',
    110: 'MSP_RX_CONFIG', 111: 'MSP_LED_STRIP_CONFIG',
    112: 'MSP_RSSI_CONFIG', 113: 'MSP_ADJUSTMENT_RANGES',
    114: 'MSP_CF_SERIAL_CONFIG', 115: 'MSP_VOLTAGE_METERS',
    116: 'MSP_SONAR', 117: 'MSP_ARMING_CONFIG',
    118: 'MSP_RX_MAP', 119: 'MSP_LOOP_TIME',
    120: 'MSP_FAILSAFE_CONFIG', 121: 'MSP_RXFAIL_CONFIG',
    122: 'MSP_SDCARD', 123: 'MSP_BLACKBOX_CONFIG',
    124: 'MSP_TRANSPONDER_CONFIG', 125: 'MSP_OSD_CONFIG',
    126: 'MSP_LED_STRIP_MODECOLOR', 127: 'MSP_FEATURE_CONFIG',
    128: 'MSP_BEEPER_CONFIG', 129: 'MSP_GEOMETRY',
    130: 'MSP_ACC_TRIM', 132: 'MSP_SENSOR_ALIGNMENT',
    133: 'MSP_INAV_STATUS', 150: 'MSP_STATUS_EX',
    151: 'MSP_SENSOR_CONFIG', 152: 'MSP_ACTIVE_SENSOR_OUTPUTS',
    155: 'MSP_FILTER_CONFIG',
}

# 初始化连接序列（Betaflight Configurator 连接时发的命令）
INIT_SEQUENCE = [
    1,   # MSP_API_VERSION
    2,   # MSP_FC_VARIANT
    3,   # MSP_FC_VERSION
    4,   # MSP_BOARD_INFO
    5,   # MSP_BUILD_INFO
    10,  # MSP_NAME
    100, # MSP_STATUS_EX
]


def msp_send(cmd, payload=b''):
    """构建并发送 MSP v1 帧"""
    length = len(payload)
    cksum = length ^ cmd
    for b in payload:
        cksum ^= b
    frame = b'$M<' + bytes([length, cmd]) + payload + bytes([cksum])
    return frame


def msp_parse(data):
    """解析 MSP 响应"""
    if len(data) < 6:
        return None
    if data[0:3] != b'$M>':
        return None
    length = data[3]
    cmd = data[4]
    payload = data[5:5+length]
    cksum = data[5+length]
    # 校验
    calc = length ^ cmd
    for b in payload:
        calc ^= b
    name = MSP_CMDS.get(cmd, f'UNKNOWN({cmd})')
    return cmd, name, payload, cksum == calc


def main():
    print(f"打开发送端口 {PORT} @ {BAUD}...")
    print(f"输出文件: {OUTPUT}")
    print()

    ser = serial.Serial(PORT, BAUD, timeout=2)
    time.sleep(0.2)

    with open(OUTPUT, 'w') as f:
        f.write(f"MSP 通信捕获 - {datetime.now()}\n")
        f.write(f"端口: {PORT} @ {BAUD}\n")
        f.write("=" * 60 + "\n\n")

        for cmd in INIT_SEQUENCE:
            frame = msp_send(cmd)
            name = MSP_CMDS.get(cmd, f'UNKNOWN({cmd})')

            # 发送
            log_send = f"[SEND] {name} (cmd={cmd}): {frame.hex()}"
            print(log_send)
            f.write(log_send + '\n')
            ser.write(frame)
            time.sleep(0.1)

            # 接收响应
            resp = ser.read(256)
            if resp:
                parsed = msp_parse(resp)
                if parsed:
                    r_cmd, r_name, r_payload, r_valid = parsed
                    log_recv = (f"[RECV] {r_name} (cmd={r_cmd}): "
                               f"{resp.hex()} | payload={r_payload.hex()} "
                               f"{'✓' if r_valid else '✗'}")
                else:
                    log_recv = f"[RECV] RAW: {resp.hex()}"
                print(log_recv)
                f.write(log_recv + '\n')
            else:
                log_recv = f"[RECV] 无响应!"
                print(log_recv)
                f.write(log_recv + '\n')

            print()
            f.write('\n')

        f.write("=" * 60 + "\n")
        f.write(f"捕获完成 - {datetime.now()}\n")

    ser.close()
    print(f"\n完成！结果保存在: {OUTPUT}")
    print(f"查看: cat {OUTPUT}")


if __name__ == '__main__':
    main()
