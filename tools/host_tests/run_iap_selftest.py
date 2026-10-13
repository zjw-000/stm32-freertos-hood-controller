"""Build and test production C upgrade logic, then exercise Python sender against it.
No serial hardware is accessed. Hardware timing and electrical behavior need board tests.
"""
import argparse
import hashlib
import hmac
import os
from pathlib import Path
import random
import shutil
import struct
import subprocess
import sys
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent.parent
sys.path.insert(0,str(HERE.parent))
import iap_common as P
from send_firmware import Client

def compile_c(gcc,name,out):
    exe=out/(name+".exe")
    subprocess.run([gcc,"-std=c99","-O1","-Wall","-Wextra","-Werror", "-I"+str(ROOT/"Shared"),
                    "-I"+str(ROOT/"tools/host_test"),str(HERE/(name+".c")),"-o",str(exe)],check=True)
    return exe

class ActualCSerial:
    def __init__(self,exe,drop_kind=None):
        self.process=subprocess.Popen([str(exe),"--server"],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        self.rx=bytearray()
        self.drop_kind=drop_kind
    def write(self,data):
        self.process.stdin.write(data.hex()+"\n")
        self.process.stdin.flush()
        line=self.process.stdout.readline()
        if not line:
            raise RuntimeError("C device simulator exited")
        if data[2]==self.drop_kind:
            self.drop_kind=None
        else:
            self.rx.extend(bytes.fromhex(line.strip()))
        return len(data)
    def flush(self): pass
    @property
    def in_waiting(self):return len(self.rx)
    def read(self,n):
        out=bytes(self.rx[:n]);del self.rx[:n];return out
    def close(self):
        self.process.stdin.close();self.process.wait(timeout=5)
        if self.process.returncode:
            raise RuntimeError("C device simulator failed")
        self.process.stdout.close()

class FastClient(Client):
    def request(self,kind,payload=b"",timeout=.05):
        return super().request(kind,payload,min(timeout,.05))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--build-dir")
    args=ap.parse_args()
    gcc=shutil.which("gcc") or r"C:\msys64\ucrt64\bin\gcc.exe"
    parent=Path(args.build_dir).resolve() if args.build_dir else ROOT/"work"
    parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="iap-test-",dir=parent) as tmp:
        out=Path(tmp)
        exe=compile_c(gcc,"test_upgrade",out)
        subprocess.run([str(exe)],check=True)
        ref=compile_c(gcc,"iap_ref",out)
        rng=random.Random(42)
        for n in (0,1,4,12,52,1028,1032):
            payload=rng.randbytes(n)
            got=subprocess.check_output([str(ref),"build","2","65535",payload.hex()],text=True).strip()
            assert bytes.fromhex(got)==P.build_frame(2,65535,payload)
        sha=compile_c(gcc,"test_sha256",out)
        vectors=dict(line.split("=",1) for line in subprocess.check_output([str(sha),"--vectors"],text=True).splitlines())
        assert vectors["abc"]==hashlib.sha256(b"abc").hexdigest()
        assert vectors["million_a"]==vectors["million_a_stream1"]==hashlib.sha256(b"a"*1000000).hexdigest()
        assert vectors["rfc4231_tc1"]==vectors["rfc4231_tc1_stream"]==hmac.new(b"\x0b"*20,b"Hi There",hashlib.sha256).hexdigest()
        print("PASS C/Python protocol and SHA/HMAC vectors")
        for drop in (None,P.CMD_BEGIN,P.CMD_DATA,P.CMD_END):
            ser=ActualCSerial(exe,drop)
            try:
                data=bytearray(rng.randbytes(20000))
                struct.pack_into("<II",data,0,0x20005000,0x08002009)
                result=FastClient(ser).upgrade(bytes(data),2,session=1234,wait=2)
                assert result["confirmed"]==1 and result["active_version"]==2
            finally:
                ser.close()
        print("PASS Python sender -> production C service: normal and lost BEGIN/DATA/END ACK")
    return 0
if __name__=="__main__":sys.exit(main())
