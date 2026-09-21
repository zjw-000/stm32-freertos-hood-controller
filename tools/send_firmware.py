# -*- coding: utf-8 -*-
"""
烟机 OTA 固件串口自动发送工具
使用方法：
  1. pip install pyserial
  2. python send_firmware.py <COM端口号，如 COM3> <固件路径，如 ../OBJ/APP_crc.bin>
"""

import sys
import os
import time

def send_firmware(port, bin_path, baudrate=115200):
    try:
        import serial
    except ImportError:
        print("错误：未检测到 pyserial 库，请先执行: pip install pyserial")
        return False

    if not os.path.exists(bin_path):
        print(f"错误：固件文件不存在 -> {bin_path}")
        return False

    with open(bin_path, 'rb') as f:
        data = f.read()

    file_size = len(data)
    print(f"==========================================")
    print(f"准备发送固件: {bin_path}")
    print(f"文件大小: {file_size} 字节 (含末尾 4 字节 CRC32)")
    print(f"目标串口: {port}, 波特率: {baudrate}")
    print(f"==========================================")

    try:
        ser = serial.Serial(port, baudrate=baudrate, timeout=1)
    except Exception as e:
        print(f"打开串口 {port} 失败: {e}")
        return False

    time.sleep(0.5)
    print("开始发送固件流...")
    start_time = time.time()
    
    # 烟机下位机是 DMA 空闲中断单次缓冲，全速整包写入
    ser.write(data)
    ser.flush()
    
    elapsed = time.time() - start_time
    print(f"发送完毕！耗时: {elapsed:.2f} 秒，平均速度: {file_size / elapsed / 1024:.2f} KB/s")
    print("等待烟机下位机响应/日志 (按 Ctrl+C 退出)...")
    
    try:
        while True:
            if ser.in_waiting:
                recv = ser.read(ser.in_waiting)
                try:
                    print(recv.decode('utf-8', errors='ignore'), end='', flush=True)
                except:
                    pass
            time.sleep(0.05)
    except KeyboardInterrupt:
        print("
退出监听。")
    finally:
        ser.close()
    return True

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法: python send_firmware.py <COM口，例如 COM3> <固件路径>")
        print("示例: python send_firmware.py COM3 ../OBJ/APP_crc.bin")
        sys.exit(1)

    port = sys.argv[1]
    bin_file = sys.argv[2]
    send_firmware(port, bin_file)
