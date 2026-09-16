# -*- coding: utf-8 -*-
"""
固件CRC32预处理工具
功能：读取原始bin文件，计算CRC32并附加到文件末尾
作者：不甘心的咸鱼--闲鱼/不搭(414192836)--小红书
使用方法：python add_crc32.py <输入文件.bin> [输出文件.bin]
已申请版权，请勿倒卖！
"""

import sys
import zlib
import struct
import os

def calculate_crc32(data):
    """计算数据的CRC32值（与STM32端一致）"""
    return zlib.crc32(data) & 0xFFFFFFFF

def add_crc_to_firmware(input_file, output_file=None):
    """
    为固件文件添加CRC32校验值
    
    参数:
        input_file: 原始bin文件路径
        output_file: 输出文件路径（可选，默认为 原文件名_crc.bin）
    """
    # 检查输入文件是否存在
    if not os.path.exists(input_file):
        print(f"错误：找不到输入文件 '{input_file}'")
        return False
    
    # 生成默认输出文件名
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}_crc.bin"
    
    try:
        # 读取原始固件数据
        with open(input_file, 'rb') as f:
            firmware_data = f.read()
        
        original_size = len(firmware_data)
        print(f"原始固件大小: {original_size} 字节")
        
        # 计算CRC32
        crc_value = calculate_crc32(firmware_data)
        print(f"CRC32校验值: 0x{crc_value:08X}")
        
        # 将CRC32转换为4字节小端序
        crc_bytes = struct.pack('<I', crc_value)
        
        # 拼接固件数据和CRC32
        output_data = firmware_data + crc_bytes
        
        # 写入输出文件
        with open(output_file, 'wb') as f:
            f.write(output_data)
        
        print(f"输出文件大小: {len(output_data)} 字节")
        print(f"成功！输出文件: '{output_file}'")
        print(f"\n请使用串口助手发送 '{output_file}' 文件进行固件升级。")
        return True
        
    except Exception as e:
        print(f"错误：{e}")
        return False

def main():
    print("=" * 50)
    print("固件CRC32预处理工具")
    print("=" * 50)
    
    if len(sys.argv) < 2:
        print("\n使用方法:")
        print("  python add_crc32.py <输入文件.bin> [输出文件.bin]")
        print("\n示例:")
        print("  python add_crc32.py app.bin")
        print("  python add_crc32.py app.bin app_with_crc.bin")
        print("\n说明:")
        print("  - 输入文件：原始的APP固件bin文件")
        print("  - 输出文件：附加CRC32后的bin文件（用于发送到STM32）")
        print("  - 如不指定输出文件名，默认为 '原文件名_crc.bin'")
        return
    
    input_file = sys.argv[1]
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    print(f"\n输入文件: {input_file}")
    add_crc_to_firmware(input_file, output_file)

if __name__ == "__main__":
    main()
