# -*- coding: utf-8 -*-
"""
固件签名工具（A档 P3）
======================
对 APP 固件 bin 计算 CRC32 + HMAC-SHA256，产出清单文件 <bin>.manifest.json，
并可对已有清单做校验。

用法：
    python sign_firmware.py APP.bin                 # 生成 APP.bin.manifest.json
    python sign_firmware.py APP.bin --version 3     # 指定版本号(默认 1)
    python sign_firmware.py APP.bin --verify        # 只校验现有清单
    python sign_firmware.py APP.bin --key <64位hex> # 用自定义密钥(量产:每台一密)

说明（面试可讲）：
  - CRC32 只能防"传输/存储出错"，它是线性的，攻击者改完固件可以随手重算；
    HMAC-SHA256 需要预共享密钥，所以能挡住"拿串口塞一份改过的固件"。
  - 真正的量产做法是 Ed25519/RSA 私钥签名 + 设备内置公钥验签，
    但 Ed25519 验签实现通常 6~8KB，塞不进 8KB 的 Bootloader；
    这里按"方案B"把校验放在 APP 侧，用 HMAC 换来 1~2KB 的实现成本。
  - 密钥现在是明文编在固件里的开发占位值，正式产品必须换成产线密钥。
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from iap_common import (BEGIN_PAYLOAD_LEN, DEV_KEY, HMAC_LEN, build_begin_payload,
                        crc32, image_hmac)


def make_manifest(bin_path, version, key):
    with open(bin_path, "rb") as f:
        data = f.read()
    manifest = {
        "file": os.path.basename(bin_path),
        "size": len(data),
        "crc32": "0x%08X" % crc32(data),
        "version": version,
        "hmac_sha256": image_hmac(data, key).hex(),
        "algo": "HMAC-SHA256(32B pre-shared key)",
    }
    return manifest


def write_manifest(bin_path, manifest):
    out = bin_path + ".manifest.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    return out


def verify_manifest(bin_path, key):
    mf_path = bin_path + ".manifest.json"
    if not os.path.exists(mf_path):
        print("错误：找不到清单文件 %s" % mf_path)
        return False
    with open(mf_path, "r", encoding="utf-8") as f:
        mf = json.load(f)
    now = make_manifest(bin_path, mf.get("version", 1), key)
    ok = True
    for k in ("size", "crc32", "hmac_sha256"):
        if str(mf.get(k)).upper() != str(now.get(k)).upper():
            print("不一致 %-12s 清单=%s 实际=%s" % (k, mf.get(k), now.get(k)))
            ok = False
    print("清单校验: %s" % ("通过 ✔" if ok else "失败 ✘"))
    return ok


def main():
    ap = argparse.ArgumentParser(description="固件 CRC32 + HMAC-SHA256 签名工具")
    ap.add_argument("bin", help="APP 固件 bin 路径")
    ap.add_argument("--version", type=int, default=1, help="固件版本号(默认 1)")
    ap.add_argument("--verify", action="store_true", help="只校验已有清单，不重写")
    ap.add_argument("--key", default=None, help="64 位 hex 密钥(默认用开发预共享密钥)")
    args = ap.parse_args()

    key = bytes.fromhex(args.key) if args.key else DEV_KEY
    if len(key) != 32:
        print("错误：密钥必须 32 字节(64 位 hex)")
        return 1

    if args.verify:
        return 0 if verify_manifest(args.bin, key) else 1

    mf = make_manifest(args.bin, args.version, key)
    out = write_manifest(args.bin, mf)
    begin = build_begin_payload(int(mf["size"]), int(mf["crc32"], 16),
                                mf["version"], bytes.fromhex(mf["hmac_sha256"]))
    assert len(begin) == BEGIN_PAYLOAD_LEN

    print("=" * 58)
    print("固件: %s" % args.bin)
    print("大小: %d 字节" % mf["size"])
    print("CRC32: %s" % mf["crc32"])
    print("版本: %d" % mf["version"])
    print("HMAC: %s" % mf["hmac_sha256"])
    print("BEGIN payload(%d 字节) = %s" % (len(begin), begin.hex()))
    print("清单已写出: %s" % out)
    print("=" * 58)
    print("下一步: python send_firmware.py <COM口> %s" % args.bin)
    return 0


if __name__ == "__main__":
    sys.exit(main())
