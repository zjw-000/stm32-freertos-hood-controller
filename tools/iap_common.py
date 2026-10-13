"""IAP v3: little-endian stream framing and authenticated image manifest."""
import hashlib
import hmac
import struct
import zlib
CHUNK_SIZE = 1024
FRAME_HDR_LEN = 7
HMAC_LEN = 32
BEGIN_PAYLOAD_LEN = 52
DATA_PAYLOAD_LEN = CHUNK_SIZE + 8
BOARD_ID = 0x103C8001
FW_MAX_SIZE = 56 * 1024
CMD_BEGIN, CMD_DATA, CMD_END, CMD_ABORT, CMD_BURN_GOLDEN, CMD_STATUS = range(1, 7)
RSP_ACK, RSP_NAK, RSP_STATUS = 0x81, 0x82, 0x86
ERR_NONE, ERR_CRC, ERR_OFFSET, ERR_STATE, ERR_SIZE, ERR_HMAC, ERR_FLASH, ERR_VERSION, ERR_BOARD, ERR_SESSION, ERR_DENIED = range(11)
ERR_TEXT = dict(enumerate(["OK", "CRC", "OFFSET", "STATE", "SIZE", "HMAC", "FLASH", "VERSION", "BOARD", "SESSION", "DENIED"]))
DEV_KEY = b"GA-IAP-DEV-KEY-CHANGE-ME-32BYTES"
def crc32(data):
    return zlib.crc32(data) & 0xffffffff

def manifest_header(data, version, board=BOARD_ID):
    return struct.pack("<IIII", len(data), crc32(data), version, board)

def image_hmac(data, key=DEV_KEY, version=1, board=BOARD_ID):
    return hmac.new(key, manifest_header(data, version, board) + data, hashlib.sha256).digest()

def build_frame(kind, seq, payload=b""):
    if len(payload) > DATA_PAYLOAD_LEN:
        raise ValueError("payload too large")
    body = struct.pack("<BHH", kind, seq & 0xffff, len(payload)) + payload
    return b"\xa5\x5a" + body + struct.pack("<I", crc32(body))

def parse_frames(buf):
    frames = []
    while buf:
        start = buf.find(b"\xa5\x5a")
        if start < 0:
            return frames, buf[-1:] if buf[-1:] == b"\xa5" else b""
        buf = buf[start:]
        if len(buf) < 7:
            break
        kind, seq, size = struct.unpack_from("<BHH", buf, 2)
        if size > DATA_PAYLOAD_LEN:
            buf = buf[1:]
            continue
        total = 11 + size
        if len(buf) < total:
            break
        if crc32(buf[2:total-4]) != struct.unpack_from("<I", buf, total-4)[0]:
            buf = buf[1:]
            continue
        frames.append((kind, seq, buf[7:total-4]))
        buf = buf[total:]
    return frames, buf

def build_begin_payload(data, version, session, key=DEV_KEY, board=BOARD_ID):
    return struct.pack("<I", session) + manifest_header(data, version, board) + image_hmac(data, key, version, board)

def build_data_payload(offset, data, session):
    return struct.pack("<II", session, offset) + data

def build_end_payload(size, crc, session):
    return struct.pack("<III", session, size, crc)

def load_key(path=None):
    if path is None:
        return DEV_KEY
    from pathlib import Path
    key = bytes.fromhex(Path(path).read_text().strip())
    if len(key) != 32:
        raise ValueError("key file must contain 32 bytes encoded as hex")
    return key

def validate_image(data):
    if not 8 <= len(data) <= FW_MAX_SIZE:
        raise ValueError("APP size must be 8..57344 bytes")
    sp, pc = struct.unpack_from("<II", data)
    if not (0x20000000 < sp <= 0x20005000 and sp % 8 == 0 and pc & 1 and 0x08002000 <= (pc & ~1) < 0x08002000 + len(data)):
        raise ValueError("APP vector invalid: build/link image at 0x08002000")
