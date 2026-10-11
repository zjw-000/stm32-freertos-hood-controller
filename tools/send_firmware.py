# -*- coding: utf-8 -*-
"""
烟机 OTA 固件串口发送工具（A档 P1，配套 Shared/iap_proto.h 的 v2 帧协议）
==========================================================================
与旧版(裸流 + 4KB 上限)的区别：
  - 走 BEGIN / DATA / END 三段式帧协议，每帧带 seq + CRC32，逐帧 ACK/NAK；
  - DATA 按 1024 字节分片、携带偏移，彻底解除"4KB 一次性接收"的上限；
  - BEGIN 携带整镜像 HMAC-SHA256，APP 侧 END 阶段回读验签，防"塞入改过的固件"。

用法：
    pip install pyserial
    python send_firmware.py COM3 ../OBJ/APP.bin
    python send_firmware.py COM3 ../OBJ/APP.bin --version 3 --verbose

没有硬件时可以先跑离线自测：
    python host_tests/run_iap_selftest.py
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iap_common import (CHUNK_SIZE, CMD_BEGIN, CMD_DATA, CMD_END, DATA_PAYLOAD_LEN,
                        DEV_KEY, ERR_NONE, ERR_TEXT, FRAME_HDR_LEN, RSP_ACK, RSP_NAK,
                        build_begin_payload, build_data_payload, build_end_payload,
                        build_frame, crc32, image_hmac, parse_frames)

ACK_TIMEOUT = 2.0      # 单帧 ACK 等待时间(秒)
END_TIMEOUT = 15.0     # END 帧要等 APP 回读整个镜像重算 HMAC，给足时间
MAX_RETRY = 3


class OtaError(Exception):
    pass


def _wait_ack(ser, seq, timeout, verbose):
    """等待 seq 对应的 ACK/NAK；返回 (ok, err)。串口上混入的调试打印会被自动跳过。"""
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
                if verbose:
                    print("    <- NAK  seq=%d err=0x%02X(%s)" % (fseq, err, ERR_TEXT.get(err, "?")))
                return False, err
        if not n:
            time.sleep(0.005)
    if verbose:
        print("    <- 超时(未收到 seq=%d 的应答)" % seq)
    return False, None


def _send_frame_expect_ack(ser, frame, seq, timeout, verbose, what):
    """发一帧并等应答。重传策略（这是"ACK 丢失怎么办"的答案）：
       - 收不到任何应答（超时/丢 ACK）-> 重传，设备侧靠"重传幂等"补 ACK；
       - 收到 NAK  -> 设备明确拒绝了，重发同一帧毫无意义，直接带错误码返回；
       - 唯一例外是 ERR_FLASH：片外 Flash 擦写偶发失败，值得重试一次。
    """
    err = None
    for attempt in range(1, MAX_RETRY + 1):
        ser.write(frame)
        ser.flush()
        ok, e = _wait_ack(ser, seq, timeout, verbose)
        if ok:
            return True, ERR_NONE
        if e is not None and e != ERR_NONE:
            if err is None:
                err = e                     # 记住第一个(最有信息量的)错误码
            if e != 0x06:                   # 只有 Flash 错误才重试
                return False, err
        if verbose:
            print("    .. %s 第 %d 次重试" % (what, attempt))
    return False, err

# 
def _run_session(ser, data, fw_crc, fw_mac, total, version, chunk, verbose):
    """一次完整的升级会话(BEGIN -> DATA... -> END)。返回 (ok, err, stage)"""
    n_chunks = (total + chunk - 1) // chunk

    # ---------- 1) BEGIN ----------
    begin = build_frame(CMD_BEGIN, 0,
                        build_begin_payload(total, fw_crc, version, fw_mac))
    print("[1/3] BEGIN ...")
    ok, err = _send_frame_expect_ack(ser, begin, 0, ACK_TIMEOUT, verbose, "BEGIN")
    if not ok:
        return False, err, "BEGIN"

    # ---------- 2) DATA ----------
    print("[2/3] DATA (%d 片 x %d 字节) ..." % (n_chunks, chunk))
    seq = 1
    off = 0
    while off < total:
        part = data[off:off + chunk]
        frame = build_frame(CMD_DATA, seq & 0xFFFF, build_data_payload(off, part))
        ok, err = _send_frame_expect_ack(ser, frame, seq, ACK_TIMEOUT, verbose, "DATA")
        if not ok:
            return False, err, "DATA"
        off += len(part)
        seq += 1
        if seq % 8 == 0 or off >= total:
            sys.stdout.write("\r      进度 %5.1f%%  (%d/%d 字节)"
                             % (100.0 * off / total, off, total))
            sys.stdout.flush()
    sys.stdout.write("\n")

    # ---------- 3) END ----------
    print("[3/3] END (设备将回读整镜像重算 HMAC，请稍候) ...")
    end = build_frame(CMD_END, seq & 0xFFFF, build_end_payload(total, fw_crc))
    ok, err = _send_frame_expect_ack(ser, end, seq, END_TIMEOUT, verbose, "END")
    return ok, err, "END"

# 
def send_firmware(port, bin_path, baudrate=115200, version=1, key=DEV_KEY,
                  chunk=CHUNK_SIZE, verbose=False):
    try:
        import serial
    except ImportError:
        print("请先安装 pyserial:  pip install pyserial")
        return False

    if chunk > CHUNK_SIZE:
        print("分片不能超过 %d 字节(受下位机缓冲限制)" % CHUNK_SIZE)
        return False

    with open(bin_path, "rb") as f:
        data = f.read()
    if not data:
        print("固件为空: %s" % bin_path)
        return False

    fw_crc = crc32(data)
    fw_mac = image_hmac(data, key)
    total = len(data)
    n_chunks = (total + chunk - 1) // chunk

    print("=" * 62)
    print("固件    : %s" % bin_path)
    print("大小    : %d 字节 (%d 片 x %d)" % (total, n_chunks, chunk))
    print("CRC32   : 0x%08X" % fw_crc)
    print("版本    : %d" % version)
    print("HMAC    : %s" % fw_mac.hex())
    print("串口    : %s @ %d" % (port, baudrate))
    print("=" * 62)

    try:
        ser = serial.Serial(port, baudrate, timeout=0.05)
    except Exception as e:
        print("打开串口失败: %s" % e)
        return False

    t0 = time.time()
    try:
        for attempt in (1, 2):
            if attempt == 2:
                print("[重试] 上一轮会话失步(设备侧被复位/丢包)，从 BEGIN 重发整个固件 ...")
            ok, err, stage = _run_session(ser, data, fw_crc, fw_mac, total,
                                          version, chunk, verbose)
            if ok:
                print("=" * 62)
                print("升级包下发完成，耗时 %.2fs" % (time.time() - t0))
                print("设备已写入 PENDING_APPLY 元数据并复位，Bootloader 将搬运+试运行；")
                print("试运行期间若 APP 不自证健康(或跑飞)，BL 会自动回滚到旧固件。")
                print("=" * 62)
                return True
            if stage == "END" and err == 0x05:
                print("END 失败: HMAC 验签不通过 —— 固件被改过，或密钥与"
                      " Shared/iap_hmac_key.h 不一致")
                return False
            if stage == "DATA" and err in (0x02, 0x03) and attempt == 1:
                continue          # 偏移失步/状态非法：重开一次会话(幂等重传仍失败)
            print("%s 失败: %s" % (stage, ERR_TEXT.get(err, "无应答(设备是否在升级态?)")))
            return False
        return False
    finally:
        ser.close()


def main():
    ap = argparse.ArgumentParser(description="烟机 OTA 固件串口发送工具(v2 帧协议)")
    ap.add_argument("port", help="串口号，如 COM3")
    ap.add_argument("bin", help="固件 bin 路径")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--version", type=int, default=1)
    ap.add_argument("--chunk", type=int, default=CHUNK_SIZE)
    ap.add_argument("--key", default=None, help="64 位 hex 预共享密钥")
    ap.add_argument("--verbose", "-v", action="store_true")
    args = ap.parse_args()

    key = bytes.fromhex(args.key) if args.key else DEV_KEY
    ok = send_firmware(args.port, args.bin, args.baud, args.version, key,
                       args.chunk, args.verbose)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
