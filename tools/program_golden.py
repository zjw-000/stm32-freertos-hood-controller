"""Provision golden once: hold KEY1 at power-on and keep it held until command completes."""
import argparse
from send_firmware import Client
from iap_common import CMD_BURN_GOLDEN

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("port")
    ap.add_argument("--baud",type=int,default=115200)
    args=ap.parse_args()
    import serial
    try:
        with serial.Serial(args.port,args.baud,timeout=.05,write_timeout=5) as ser:
            Client(ser).request(CMD_BURN_GOLDEN,timeout=15)
            print("Golden image written and verified.")
    except (OSError,RuntimeError,TimeoutError) as exc:
        ap.exit(1,str(exc)+"\n")
if __name__=="__main__": main()
