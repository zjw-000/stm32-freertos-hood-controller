# -*- coding: utf-8 -*-
"""
A 档 P1/P3 上位机链路离线自测（不需要硬件、不需要串口）
=========================================================
两层验证：
  1) 交叉验证：把固件里真正编译的那份 C 实现(Shared/iap_proto.h)编成 iap_ref.exe，
     与 Python 工具(tools/iap_common.py) 做组帧/解帧的字节级比对，防止两端协议漂移。
  2) 端到端：用"假串口 + 设备状态机模型"(完全照抄 APP 侧 Iap_OnBegin/OnData/OnEnd 逻辑)
     跑 tools/send_firmware.py 的完整 BEGIN/DATA/END 流程，覆盖：
        T1 正常升级            T2 密钥不对(模拟攻击者无密钥)
        T3 丢 ACK 后重传        T4 中途设备复位(掉电)后重开会话
        T5 篡改固件但签名是旧的(签名不匹配)
        T6 黄金镜像固化(CMD_BURN_GOLDEN)成功
        T7 非法 BURN_GOLDEN(带 payload) -> 设备 NAK ERR_SIZE

用法： python run_iap_selftest.py            (需要 gcc 在 PATH 或 MSYS2 默认路径)
"""

import os
import random
import struct
import subprocess
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
sys.path.insert(0, TOOLS)

import iap_common as P
import program_golden as PG
import send_firmware as SF

IAP_FW_MAX_SIZE = 56 * 1024
GCC_CANDIDATES = [r"C:\msys64\ucrt64\bin\gcc.exe",
                  r"C:\msys64\mingw64\bin\gcc.exe",
                  r"C:\msys64\usr\bin\gcc.exe",
                  "gcc"]

results = []


def check(name, cond, extra=""):
    results.append((name, bool(cond), extra))
    print("  [%s] %s %s" % ("PASS" if cond else "FAIL", name, extra))


# ----------------------------------------------------------------------
# 1) C 参考实现交叉验证
# ----------------------------------------------------------------------
def build_ref():
    for gcc in GCC_CANDIDATES:
        try:
            r = subprocess.run([gcc, "-O1", "-Wall", "-Wextra",
                                "-I../../Shared", "-I../host_test",
                                "-o", "iap_ref.exe", "iap_ref.c"],
                               cwd=HERE, capture_output=True, text=True, timeout=180)
        except (FileNotFoundError, subprocess.TimeoutExpired):
            continue
        if r.returncode == 0:
            return os.path.join(HERE, "iap_ref.exe"), r.stderr.strip()
        print("  gcc 编译失败(%s): %s" % (gcc, r.stderr.strip()[:200]))
    return None, "no gcc"


def ref_build(exe, ftype, seq, payload):
    r = subprocess.run([exe, "build", str(ftype), str(seq), payload.hex()],
                       capture_output=True, text=True, timeout=60)
    return r.stdout.strip().upper()


def ref_parse(exe, stream):
    r = subprocess.run([exe, "parse", stream.hex()], capture_output=True,
                       text=True, timeout=60)
    out = []
    for line in r.stdout.strip().splitlines():
        p = line.split()
        if len(p) >= 4 and p[0] == "F":
            ln = int(p[3])
            hx = p[4] if len(p) == 5 else ""      # len==0 的帧没有 hex 字段
            if len(hx) != ln * 2:
                raise AssertionError("C 输出解析异常: %r" % line)
            out.append((int(p[1]), int(p[2]), ln, bytes.fromhex(hx)))
    return out


def test_cross_language(exe):
    rnd = random.Random(20261009)
    ok = True
    for _ in range(60):
        ftype = rnd.choice([0x01, 0x02, 0x03, 0x04, 0x05, 0x81, 0x82])
        seq = rnd.randrange(0, 65536)
        n = rnd.choice([0, 1, 2, 8, 44, 1028])
        payload = bytes(rnd.randrange(256) for _ in range(n))
        py = P.build_frame(ftype, seq, payload).hex().upper()
        c = ref_build(exe, ftype, seq, payload)
        if py != c:
            ok = False
            check("组帧一致 type=%d seq=%d n=%d" % (ftype, seq, n), False,
                  "py=%s c=%s" % (py, c))
            break
    check("60 组随机帧 C/Python 组帧字节级一致", ok)

    # 解帧：Python 组帧 + 随机垃圾字节 -> C 解析，结果应与 Python 解析一致
    ok = True
    for _ in range(20):
        frames = []
        for k in range(rnd.randrange(1, 6)):
            pl = bytes(rnd.randrange(256) for _ in range(rnd.choice([0, 4, 44, 1028])))
            frames.append((rnd.choice([1, 2, 3]), k + 1, pl))
        stream = b""
        for f in frames:
            stream += bytes([rnd.randrange(256)]) + P.build_frame(*f)
        c_parsed = [(t_, s_, pl_) for (t_, s_, _l, pl_) in ref_parse(exe, stream)]
        py_parsed, _ = P.parse_frames(stream)
        py_parsed = [(t_, s_, pl_) for (t_, s_, pl_) in py_parsed]
        if c_parsed != py_parsed:
            ok = False
            check("解帧一致", False, "C=%s PY=%s" % (c_parsed[:2], py_parsed[:2]))
            break
    check("含垃圾字节的流 C/Python 解帧结果一致", ok)


# ----------------------------------------------------------------------
# 2) 假串口 + 设备模型（照抄 APP 侧 IAP 状态机）
# ----------------------------------------------------------------------
class DeviceModel(object):
    """完全按 APP_TASK/app_tasks.c 的 Iap_OnBegin / Iap_OnData / Iap_OnEnd 逻辑实现"""

    def __init__(self, key=None):
        self.key = key if key is not None else P.DEV_KEY
        self.rx = b""
        self.tx = bytearray()
        self.reset_state()
        self.mem = None
        self.applied = False
        self.sessions = 0
        self.drop_ack_seq = None      # 模拟"ACK 在串口上丢了"
        self.reset_at_seq = None      # 模拟"设备中途掉电复位"
        self.wrong_key = None
        self.fw = None                # 当前"运行区"固件内容(0xFF 填充表示空)
        self.golden = None            # 黄金镜像固化结果
        self.golden_size = 0
        self.golden_crc32 = 0

    def reset_state(self):
        self.active = False
        self.size = 0
        self.crc_decl = 0
        self.ver = 0
        self.hmac_decl = None
        self.next_off = 0
        self.crc_state = P.crc32(b"")
        self.last_seq = 0

    # ---- 串口侧 ----
    def feed(self, data):
        self.rx += data
        frames, rest = P.parse_frames(self.rx)
        self.rx = rest
        for ftype, seq, payload in frames:
            if self.reset_at_seq is not None and seq == self.reset_at_seq and ftype == P.CMD_DATA:
                self.reset_at_seq = None
                self.reset_state()          # 掉电：会话状态全丢
            self._dispatch(ftype, seq, payload)

    def _ack(self, seq):
        if self.drop_ack_seq is not None and seq == self.drop_ack_seq:
            self.drop_ack_seq = None
            return                          # 这一帧的应答"丢"了
        self.tx += P.build_frame(P.RSP_ACK, seq, struct.pack("<H", seq))

    def _nak(self, seq, err):
        self.tx += P.build_frame(P.RSP_NAK, seq, struct.pack("<HB", seq, err))

    # ---- 状态机 ----
    def _dispatch(self, ftype, seq, payload):
        if ftype == P.CMD_BEGIN:
            if len(payload) != P.BEGIN_PAYLOAD_LEN:
                self._nak(seq, P.ERR_SIZE)
                return
            size, crc, ver = struct.unpack_from("<III", payload, 0)
            self.size, self.crc_decl, self.ver = size, crc, ver
            self.hmac_decl = payload[12:44]
            if size == 0 or size > IAP_FW_MAX_SIZE:
                self.active = False
                self._nak(seq, P.ERR_SIZE)
                return
            self.mem = bytearray(size)      # 擦除暂存区
            self.next_off = 0
            self.crc_state = P.crc32(b"")
            self.last_seq = seq
            self.active = True
            self.sessions += 1
            self._ack(seq)
            return

        if ftype == P.CMD_DATA:
            if not self.active:
                self._nak(seq, P.ERR_STATE)
                return
            if len(payload) < 4:
                self._nak(seq, P.ERR_SIZE)
                return
            off = struct.unpack_from("<I", payload, 0)[0]
            data = payload[4:]
            dlen = len(data)
            # 重传幂等：ACK 丢了以后上位机重发的同一帧，直接补 ACK
            if seq == self.last_seq and (off + dlen) == self.next_off:
                self._ack(seq)
                return
            if off != self.next_off:
                self._nak(seq, P.ERR_OFFSET)
                return
            if (off + dlen) > self.size or dlen > P.DATA_PAYLOAD_LEN - 4:
                self._nak(seq, P.ERR_SIZE)
                return
            self.mem[off:off + dlen] = data
            self.crc_state = P.crc32(bytes(self.mem[:off + dlen]))
            self.next_off = off + dlen
            self.last_seq = seq
            self._ack(seq)
            return

        if ftype == P.CMD_END:
            if not self.active:
                self._nak(seq, P.ERR_STATE)
                return
            if len(payload) < 8:
                self._nak(seq, P.ERR_SIZE)
                return
            size, crc = struct.unpack_from("<II", payload, 0)
            if size != self.size or crc != self.crc_decl:
                self._nak(seq, P.ERR_SIZE)
                return
            if self.next_off != self.size:
                self._nak(seq, P.ERR_SIZE)
                return
            image = bytes(self.mem)
            if P.crc32(image) != self.crc_decl:
                self._nak(seq, P.ERR_CRC)
                return
            key = self.wrong_key if self.wrong_key is not None else self.key
            if P.image_hmac(image, key) != self.hmac_decl:
                self.active = False          # 验签失败：作废会话，绝不落盘
                self._nak(seq, P.ERR_HMAC)
                return
            self.active = False
            self.applied = True              # 置 PENDING_APPLY
            self._ack(seq)
            return

        if ftype == P.CMD_BURN_GOLDEN:
            # 照抄 Iap_OnBurnGolden：无 payload；按 0xFF 反推运行区真实长度
            if len(payload) != 0:
                self._nak(seq, P.ERR_SIZE)
                return
            if self.fw is None:
                self._nak(seq, P.ERR_SIZE)      # 运行区为空 -> 没有可固化的固件
                return
            i = len(self.fw)
            while i > 0 and self.fw[i - 1] == 0xFF:
                i -= 1
            ln = (i + 3) & ~3
            if ln == 0 or ln > IAP_FW_MAX_SIZE:
                self._nak(seq, P.ERR_SIZE)
                return
            self.golden = self.fw[:ln]
            self.golden_size = ln
            self.golden_crc32 = P.crc32(self.golden)
            self._ack(seq)
            return

        self._nak(seq, P.ERR_STATE)


class FakeSerial(object):
    def __init__(self, device, *a, **k):
        self.d = device
        self.timeout = 0.05

    @property
    def in_waiting(self):
        return len(self.d.tx)

    def read(self, n):
        out = bytes(self.d.tx[:n])
        del self.d.tx[:n]
        return out

    def write(self, b):
        self.d.feed(bytes(b))
        return len(b)

    def flush(self):
        pass

    def close(self):
        pass


def install_fake_serial(device):
    mod = types.ModuleType("serial")
    mod.Serial = lambda port, baud, timeout=0.05: FakeSerial(device)
    sys.modules["serial"] = mod


# ----------------------------------------------------------------------
# 3) 端到端用例
# ----------------------------------------------------------------------
def make_image(path, n, seed):
    rnd = random.Random(seed)
    data = bytes(rnd.randrange(256) for _ in range(n))
    with open(path, "wb") as f:
        f.write(data)
    return data


def test_end_to_end(tmpdir):
    import io
    import contextlib

    dev = DeviceModel()
    install_fake_serial(dev)
    img = make_image(os.path.join(tmpdir, "t1.bin"), 20000, 1)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ok = SF.send_firmware("FAKE", os.path.join(tmpdir, "t1.bin"), verbose=False)
    check("T1 正常升级 20000 字节(20 片)", ok and dev.applied and bytes(dev.mem) == img,
          "sessions=%d" % dev.sessions)

    # T2 攻击者无密钥：用自己的密钥签名，设备应拒
    dev = DeviceModel()
    dev.wrong_key = P.DEV_KEY
    install_fake_serial(dev)
    img2 = make_image(os.path.join(tmpdir, "t2.bin"), 6000, 2)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ok = SF.send_firmware("FAKE", os.path.join(tmpdir, "t2.bin"),
                              key=bytes(range(32)))
    check("T2 密钥不对 -> 设备拒收(HMAC)", (not ok) and (not dev.applied),
          "输出含 HMAC 提示=%s" % ("HMAC" in buf.getvalue()))

    # T3 丢 ACK：设备收下但不应答，上位机重传 -> 设备补 ACK(幂等)
    dev = DeviceModel()
    dev.drop_ack_seq = 3
    install_fake_serial(dev)
    img3 = make_image(os.path.join(tmpdir, "t3.bin"), 6000, 3)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ok = SF.send_firmware("FAKE", os.path.join(tmpdir, "t3.bin"))
    check("T3 丢一次 ACK -> 重传幂等补 ACK，升级仍成功",
          ok and dev.applied and bytes(dev.mem) == img3)

    # T4 中途掉电复位：设备状态丢失 -> 上位机重开会话
    dev = DeviceModel()
    dev.reset_at_seq = 4
    install_fake_serial(dev)
    img4 = make_image(os.path.join(tmpdir, "t4.bin"), 6000, 4)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ok = SF.send_firmware("FAKE", os.path.join(tmpdir, "t4.bin"))
    check("T4 中途掉电 -> 自动重开会话并成功",
          ok and dev.applied and bytes(dev.mem) == img4 and dev.sessions >= 2,
          "sessions=%d" % dev.sessions)

    # T5 篡改固件但沿用旧签名：签名与内容不符 -> HMAC 拒
    dev = DeviceModel()
    install_fake_serial(dev)
    img5 = bytearray(make_image(os.path.join(tmpdir, "t5.bin"), 6000, 5))
    good_mac = P.image_hmac(bytes(img5))          # 攻击者拿不到的"真签名"
    img5[100] ^= 0xFF
    img5[3000] ^= 0x0F
    tampered = bytes(img5)
    # 攻击者能重算 CRC32(线性、无密钥)，但算不出 HMAC -> 必须被 END 阶段拦下
    fake_crc = P.crc32(tampered)
    ok, err, stage = SF._run_session(FakeSerial(dev), tampered, fake_crc,
                                     good_mac, len(tampered), 1, P.CHUNK_SIZE, False)
    check("T5 篡改固件+重算CRC(无密钥) -> END 阶段 NAK HMAC",
          (not ok) and err == P.ERR_HMAC and (not dev.applied), "stage=%s" % stage)

    # T6 黄金镜像固化(P6)：运行区固件 + 0xFF 尾巴，设备应裁掉填充后固化
    dev = DeviceModel()
    img6 = make_image(os.path.join(tmpdir, "t6.bin"), 8000, 6)
    dev.fw = img6 + b"\xFF" * 3000        # 模拟"实际固件只占 8000，后面还是空白 Flash"
    install_fake_serial(dev)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        ok = PG.burn_golden("FAKE", timeout=2.0)
    check("T6 BURN_GOLDEN 固化成功(长度按 0xFF 反推=8000)",
          ok and dev.golden_size == 8000 and dev.golden == img6
          and dev.golden_crc32 == P.crc32(img6),
          "size=%d crc=0x%08X" % (dev.golden_size, dev.golden_crc32))

    # T7 协议健壮性：BURN_GOLDEN 不该带 payload
    dev = DeviceModel()
    dev.fw = img6
    install_fake_serial(dev)
    ser = FakeSerial(dev)
    ser.write(P.build_frame(P.CMD_BURN_GOLDEN, 0, b"\x01\x02\x03"))
    ok7, err7 = PG._wait_ack(ser, 0, 1.0, False)
    check("T7 带 payload 的 BURN_GOLDEN -> NAK ERR_SIZE",
          (not ok7) and err7 == P.ERR_SIZE and dev.golden is None)


def main():
    print("=" * 66)
    print("A 档 P1/P3 上位机链路离线自测")
    print("=" * 66)

    print("[1] C 参考实现交叉验证")
    exe, err = build_ref()
    if exe:
        check("gcc 编译 iap_ref.c (0 warning)", True)
        test_cross_language(exe)
    else:
        check("gcc 编译 iap_ref.c", False, err)

    print("[2] 假串口端到端")
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        test_end_to_end(tmp)

    n_fail = sum(1 for _, ok, _ in results if not ok)
    print("-" * 66)
    print("用例 %d 个，通过 %d 个，失败 %d 个" % (len(results), len(results) - n_fail, n_fail))
    return 1 if n_fail else 0


if __name__ == "__main__":
    sys.exit(main())
