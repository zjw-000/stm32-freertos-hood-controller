"""Generate an authenticated v3 manifest; does not modify the binary."""
import argparse
import json
from pathlib import Path
import iap_common as P

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("image")
    ap.add_argument("--version",type=int,required=True)
    ap.add_argument("--key-file")
    ap.add_argument("--production",action="store_true")
    ap.add_argument("--output")
    args=ap.parse_args()
    if args.production and not args.key_file:
        ap.error("--production requires --key-file")
    if not 1 <= args.version <= 0xffffffff:
        ap.error("version must be 1..4294967295")
    data=Path(args.image).read_bytes()
    P.validate_image(data)
    manifest=dict(protocol=3,board_id=P.BOARD_ID,version=args.version,size=len(data),crc32=P.crc32(data),
                  hmac_sha256=P.image_hmac(data,P.load_key(args.key_file),args.version).hex())
    path=Path(args.output or args.image+".manifest.json")
    path.write_text(json.dumps(manifest,indent=2)+"\n",encoding="utf-8")
    print(path)
if __name__=="__main__": main()
