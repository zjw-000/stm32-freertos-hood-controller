# -*- coding: utf-8 -*-
"""
黄金镜像固化工具（A档 P6 配套，对应 IAP_CMD_BURN_GOLDEN = 0x05）
================================================================================
为什么需要它：
    黄金镜像是 Bootloader 回滚链条上的**最后一道防线**——当"新固件连续启动失败"
    且"备份区也坏/为空"时，BL 只能退到黄金镜像。而黄金镜像不是编译产物，是**在
    现场把某一台确认好用的机器上的固件"定格"下来**，所以必须由上位机触发。

设备侧做的事（APP_TASK/app_tasks.c -> Iap_OnBurnGolden）：
    1) 从 MCU_APP_ADDR(0x08002000) 起按 0xFF 反推当前运行固件的真实长度；
    2) 擦除 SPI Flash 黄金区(SPI_GOLDEN_ADDR)；
    3) 分块把运行区固件拷贝进黄金区，同时流式算 CRC32；
    4) 读两份元数据、选最新的一份，更新 golden_size / golden_crc32，
       写进"下一个"元数据槽（双份+序号，掉电不丢）。

上位机做的事：
    只发一帧 CMD_BURN_GOLDEN（无 payload），然后等 ACK —— 设备在固化期间是阻塞的，
    所以等待时间给得比普通帧长得多（默认 30s，随固件大小线性增长，约 48KB 需数秒）。

注意：
    * 该操作**会写 SPI Flash**，不要在"正在跑一个可能有问题的新固件"时执行，
      否则等于把坏固件定格成黄金镜像；正确时机是"新固件试运行确认 OK 之后"。
    * 超时后**不要盲目重发**：每次成功固化都会占用一个新的元数据槽（擦写 SPI Flash
      元数据扇区），先确认设备是否已复位/串口是否被占用，再人工决定是否重来。

用法：
    pip install pyserial
    python program_golden.py COM3
    python program_golden.py COM3 --timeout 60 --verbose
    python program_golden.py --dry-run            # 不连硬件，只看这一帧长什么样
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iap_common import (CMD_BURN_GOLDEN, ERR_NONE, ERR_TEXT, RSP_ACK, RSP_NAK,
                        build_frame, parse_frames)

BURN_TIMEOUT = 30.0        # 固化一帧的等待上限(秒)：擦 12 个扇区 + 写 192 页
POLL_INTERVAL = 0.05


def build_burn_golden_frame(seq=0):
    """CMD_BURN_GOLDEN 帧：无 payload，长度 = 2(头) + 7(帧头) + 4(CRC) = 13 字节"""
    return build_frame(CMD_BURN_GOLDEN, seq, b"")


def _wait_ack(ser, seq, timeout, verbose):
    """等 seq 对应的 ACK/NAK；返回 (ok, err)。串口上混入的调试打印会被自动跳过。"""
    buf = b""
    deadline = time.time() + timeout
    while time.time() < deadline:
        n = ser.in_waiting
        if n:
            buf += ser.read(n)
        frames, buf = parse_frames(buf)
        for ftype, fseq, payload in frames:
            if fseq != (seq & 0xFFFF):
                continue
            if ftype == RSP_ACK:
                if verbose:
                    print("    <- ACK  seq=%d" % fseq)
                return True, ERR_NONE
            if ftype == RSP_NAK:
                err = payload[2] if len(payload) >= 3 else 0xFF
                print("    <- NAK  seq=%d err=0x%02X(%s)"
                      % (fseq, err, ERR_TEXT.get(err, "?")))
                return False, err
        if not n:
            time.sleep(POLL_INTERVAL)
    return False, None


def burn_golden(port, baudrate=115200, timeout=BURN_TIMEOUT, seq=0, verbose=False):
    """向设备发一帧 CMD_BURN_GOLDEN 并等应答。返回 True/False。"""
    try:
        import serial
    except ImportError:
        print("请先安装 pyserial:  pip install pyserial")
        return False

    frame = build_burn_golden_frame(seq)
    print("=" * 62)
    print("命令    : BURN_GOLDEN (0x%02X)" % CMD_BURN_GOLDEN)
    print("帧长    : %d 字节 (无 payload)" % len(frame))
    print("串口    : %s @ %d" % (port, baudrate))
    print("等待上限: %.0f 秒（设备要擦 SPI Flash 黄金区 + 逐块拷贝 + 写元数据）" % timeout)
    print("=" * 62)

    try:
        ser = serial.Serial(port, baudrate, timeout=0.05)
    except Exception as e:
        print("打开串口失败: %s" % e)
        return False

    t0 = time.time()
    try:
        ser.write(frame)
        ser.flush()
        ok, err = _wait_ack(ser, seq, timeout, verbose)
    finally:
        ser.close()

    if ok:
        print("=" * 62)
        print("黄金镜像固化完成，耗时 %.2fs" % (time.time() - t0))
        print("设备已更新元数据 golden_size / golden_crc32（双份元数据+序号，掉电安全）。")
        print("此后 BL 在\"新固件连续启动失败\"且备份区不可用时，会尝试该黄金镜像。")
        print("=" * 62)
        return True

    if err is None:
        print("=" * 62)
        print("超时：%.0f 秒内没有收到应答。" % timeout)
        print("排查顺序：")
        print("  1) 串口是否被别的工具占用 / 设备是否已复位（APP 不在跑就没有这个命令）；")
        print("  2) 用 --timeout 调大再试（固件越大，擦写越久）；")
        print("  3) 确认上位机跑的是 APP 固件（BL 不处理 BURN_GOLDEN），")
        print("     即升级后要先复位进 APP，再执行本工具。")
        print("注意：不要盲目连续重发 —— 每次成功固化都会占用一个新的元数据槽。")
        print("=" * 62)
    else:
        print("固化失败: %s (0x%02X)" % (ERR_TEXT.get(err, "未知错误"), err))
    return False


def dry_run(verbose=False):
    """不连硬件，打印这一帧的字节，便于与 Shared/iap_proto.h 的约定对照。"""
    frame = build_burn_golden_frame(0)
    print("CMD_BURN_GOLDEN 帧: %s" % frame.hex().upper())
    print("期望前缀 A55A05 (帧头 + type=0x05)，总长 %d = 2 + 7 + 0 + 4" % len(frame))
    if verbose:
        print("逐字节: " + " ".join("%02X" % b for b in frame))
    return True


def main():
    ap = argparse.ArgumentParser(description="黄金镜像固化工具 (CMD_BURN_GOLDEN)")
    ap.add_argument("port", nargs="?", help="串口号，如 COM3（--dry-run 时可省略）")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--timeout", type=float, default=BURN_TIMEOUT,
                    help="等待 ACK 的秒数，默认 %.0f" % BURN_TIMEOUT)
    ap.add_argument("--seq", type=int, default=0, help="帧序号，默认 0")
    ap.add_argument("--dry-run", action="store_true", help="不连硬件，只打印待发帧")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    if args.dry_run:
        return 0 if dry_run(args.verbose) else 1
    if not args.port:
        ap.error("请给出串口号，或用 --dry-run")
    ok = burn_golden(args.port, args.baud, args.timeout, args.seq, args.verbose)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
