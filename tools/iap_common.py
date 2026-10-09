# -*- coding: utf-8 -*-
"""
烟机 IAP 上位机公共模块（A档 P1/P3）
====================================
与下位机 Shared/iap_proto.h + Shared/boot_config.h 严格一一对应：
  - 帧格式： 0xA5 0x5A | type | seq(u16) | len(u16) | payload | crc32(u32)
             CRC32 覆盖 [type .. payload 末字节]，全部小端
  - 固件 CRC32：标准 zlib.crc32（与 STM32 端 BootCrc32 一致）
  - 镜像签名：HMAC-SHA256，密钥为 Shared/iap_hmac_key.h 里的预共享密钥

任何一端改了协议，这里必须同步改；tools/host_tests 里有一致性自测脚本。
"""

import binascii
import hashlib
import hmac
import struct

# ---------------- 帧常量（对应 Shared/boot_config.h） ----------------
FRAME_HEAD0 = 0xA5
FRAME_HEAD1 = 0x5A
FRAME_HDR_LEN = 7
FRAME_TAIL_LEN = 4

CMD_BEGIN = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_ABORT = 0x04
CMD_BURN_GOLDEN = 0x05

RSP_ACK = 0x81
RSP_NAK = 0x82

ERR_NONE = 0x00
ERR_CRC = 0x01
ERR_OFFSET = 0x02
ERR_STATE = 0x03
ERR_SIZE = 0x04
ERR_HMAC = 0x05
ERR_FLASH = 0x06

ERR_TEXT = {
    ERR_NONE: "NONE",
    ERR_CRC: "CRC 不匹配",
    ERR_OFFSET: "DATA 偏移不连续",
    ERR_STATE: "状态非法(未收到 BEGIN?)",
    ERR_SIZE: "长度/尺寸非法",
    ERR_HMAC: "HMAC 验签失败",
    ERR_FLASH: "片外 Flash 操作失败",
}

CHUNK_SIZE = 1024                  # 每帧 DATA 携带的固件字节数
DATA_PAYLOAD_LEN = CHUNK_SIZE + 4  # offset u32 + data
BEGIN_PAYLOAD_LEN = 12 + 32        # size + crc32 + ver + hmac[32]
MAX_PAYLOAD_LEN = 64                                   # BEGIN 帧 payload 上限
MAX_FRAME_LEN = FRAME_HDR_LEN + DATA_PAYLOAD_LEN + FRAME_TAIL_LEN   # = 1039
HMAC_LEN = 32

# ---------------- 预共享密钥（与 Shared/iap_hmac_key.h 一致） ----------------
DEV_KEY = b"GA-IAP-DEV-KEY-CHANGE-ME-32BYTES"
assert len(DEV_KEY) == 32, "DEV_KEY 必须 32 字节，且与 iap_hmac_key.h 同步"


# ---------------- 基础算法 ----------------
def crc32(data: bytes) -> int:
    """标准 CRC32（与 STM32 端 BootCrc32 一致）"""
    return binascii.crc32(data) & 0xFFFFFFFF


def image_hmac(data: bytes, key: bytes = DEV_KEY) -> bytes:
    """整镜像 HMAC-SHA256，与 APP 侧 Iap_OnEnd 的逐块流式重算结果一致"""
    return hmac.new(key, data, hashlib.sha256).digest()


# ---------------- 帧编解码 ----------------
def build_frame(ftype: int, seq: int, payload: bytes = b"") -> bytes:
    """按协议组一帧"""
    if len(payload) > 0xFFFF:
        raise ValueError("payload 过长")
    body = struct.pack("<BHH", ftype, seq & 0xFFFF, len(payload)) + payload
    frame = bytes([FRAME_HEAD0, FRAME_HEAD1]) + body + struct.pack("<I", crc32(body))
    return frame


def build_begin_payload(size: int, crc: int, version: int, mac: bytes) -> bytes:
    if len(mac) != HMAC_LEN:
        raise ValueError("HMAC 长度必须 %d" % HMAC_LEN)
    return struct.pack("<III", size, crc, version) + mac


def build_data_payload(offset: int, data: bytes) -> bytes:
    return struct.pack("<I", offset) + data


def build_end_payload(size: int, crc: int) -> bytes:
    return struct.pack("<II", size, crc)


def parse_frames(buf: bytes):
    """从缓冲里顺序解析出所有完整帧；返回 (frames, rest)
    frames: [(type, seq, payload_bytes), ...]，rest: 未构成完整帧的尾部字节
    非法帧(帧头错/CRC 错)按协议丢 1 字节重同步，与下位机行为一致。"""
    frames = []
    i = 0
    n = len(buf)
    while True:
        if n - i < FRAME_HDR_LEN:
            break
        if buf[i] != FRAME_HEAD0 or buf[i + 1] != FRAME_HEAD1:
            i += 1
            continue
        ftype, seq, length = struct.unpack_from("<BHH", buf, i + 2)
        if length > MAX_FRAME_LEN:
            # 与 C 端 IapProto_ParseFrame 完全一致：长度越界视为非法帧，丢 1 字节重同步。
            # (少了这一步，垃圾字节里出现的大长度会让解析器一直"等更多数据"而卡死)
            i += 1
            continue
        need = FRAME_HDR_LEN + length + FRAME_TAIL_LEN
        if n - i < need:
            break
        body = buf[i + 2:i + FRAME_HDR_LEN + length]
        crc_recv = struct.unpack_from("<I", buf, i + FRAME_HDR_LEN + length)[0]
        if crc32(body) != crc_recv:
            i += 1
            continue
        frames.append((ftype, seq, bytes(buf[i + FRAME_HDR_LEN:i + FRAME_HDR_LEN + length])))
        i += need
    return frames, buf[i:]


def parse_ack_nak(payload: bytes):
    """解析 ACK/NAK 的 payload -> (ack_seq, err 或 None)"""
    if len(payload) >= 3 and payload[0] is not None:
        ack_seq = struct.unpack_from("<H", payload, 0)[0]
        if len(payload) >= 3:
            return ack_seq, payload[2]
        return ack_seq, None
    return None, None
