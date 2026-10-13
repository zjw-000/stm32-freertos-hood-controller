"""Serial IAP v3. ACK means staged; success requires post-reboot healthy confirmation."""
import argparse
import secrets
import struct
import time
from pathlib import Path
import iap_common as P

ACK_TIMEOUT = 5.0
MAX_RETRY = 3

class Client:
    def __init__(self, ser):
        self.ser = ser
        self.buf = b""
        self.seq = 0

    def request(self, kind, payload=b"", timeout=ACK_TIMEOUT):
        self.seq = (self.seq + 1) & 0xffff
        frame = P.build_frame(kind, self.seq, payload)
        for _ in range(MAX_RETRY):
            self.ser.write(frame)
            self.ser.flush()
            end = time.monotonic() + timeout
            while time.monotonic() < end:
                n = self.ser.in_waiting
                if n:
                    self.buf += self.ser.read(n)
                frames, self.buf = P.parse_frames(self.buf)
                for k, seq, body in frames:
                    if seq != self.seq:
                        continue
                    if k == P.RSP_NAK:
                        raise RuntimeError("device rejected: " + P.ERR_TEXT.get(body[2] if len(body) >= 3 else -1, "invalid NAK"))
                    if k == P.RSP_ACK and (len(body) != 2 or struct.unpack("<H", body)[0] != self.seq):
                        continue
                    if k in (P.RSP_ACK, P.RSP_STATUS):
                        return k, body
                time.sleep(.005)
        raise TimeoutError("no reply; same session can be queried with --status --session")

    def status(self, session=0):
        kind, body = self.request(P.CMD_STATUS, struct.pack("<I", session), timeout=1.0)
        if kind != P.RSP_STATUS or len(body) != 24:
            raise RuntimeError("invalid status reply")
        return dict(zip(["session", "state", "confirmed", "active_version", "download_version", "version_floor"], struct.unpack("<6I", body)))

    def upgrade(self, data, version, key=P.DEV_KEY, session=None, wait=45):
        P.validate_image(data)
        if not 1 <= version <= 0xffffffff:
            raise ValueError("version must be 1..4294967295")
        sid = session if session is not None else secrets.randbelow(0xffffffff) + 1
        print("session=%d version=%d bytes=%d" % (sid, version, len(data)))
        self.request(P.CMD_BEGIN, P.build_begin_payload(data, version, sid, key), timeout=15.0)
        for off in range(0, len(data), P.CHUNK_SIZE):
            self.request(P.CMD_DATA, P.build_data_payload(off, data[off:off+P.CHUNK_SIZE], sid))
        # END ACK can be lost across reboot. Query durable status even if it times out.
        try:
            self.request(P.CMD_END, P.build_end_payload(len(data), P.crc32(data), sid), timeout=5.0)
        except TimeoutError:
            pass
        end = time.monotonic() + wait
        while time.monotonic() < end:
            try:
                status = self.status(sid)
                if status["state"] == 0 and status["confirmed"]:
                    if status["active_version"] != version:
                        raise RuntimeError("device rolled back; requested version is not active")
                    print("healthy confirmation received:", status)
                    return status
                if status["state"] == 5:
                    raise RuntimeError("device entered recovery")
            except TimeoutError:
                pass
            time.sleep(.5)
        raise TimeoutError("staged but healthy boot not confirmed; inspect board or query session %d" % sid)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port")
    ap.add_argument("image", nargs="?")
    ap.add_argument("--version", type=int)
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--key-file")
    ap.add_argument("--production", action="store_true")
    ap.add_argument("--session", type=int)
    ap.add_argument("--status", action="store_true")
    args = ap.parse_args()
    if not args.status and (not args.image or args.version is None):
        ap.error("upgrade requires image and --version; version must exceed device floor")
    if args.production and not args.key_file:
        ap.error("--production requires --key-file")
    import serial
    try:
        with serial.Serial(args.port, args.baud, timeout=.05, write_timeout=5) as ser:
            cli = Client(ser)
            if args.status:
                print(cli.status(args.session or 0))
            else:
                cli.upgrade(Path(args.image).read_bytes(), args.version, P.load_key(args.key_file), args.session)
    except (OSError, ValueError, RuntimeError, TimeoutError) as exc:
        ap.exit(1, str(exc) + "\n")
if __name__ == "__main__":
    main()
