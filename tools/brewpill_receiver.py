#!/usr/bin/env python3
"""Listen for brew pill BLE broadcasts and print (or log) the readings.

Works on Linux (including Raspberry Pi), macOS and Windows.

    pip install bleak
    python3 brewpill_receiver.py
    python3 brewpill_receiver.py --csv log.csv
    python3 brewpill_receiver.py --poly 1.0012 -0.00123 0.0000456 0

--poly converts tilt to specific gravity on the receiver side using
SG = C0 + C1*a + C2*a^2 + C3*a^3 (from calibrate.py), which lets you
calibrate without reflashing the pill.
"""

import argparse
import asyncio
import csv
import datetime
import struct
import sys

COMPANY_ID = 0xFFFF
MAGIC = b"BP"

STATUS_ACCEL_ERROR = 0x01
STATUS_SG_UNCALIBRATED = 0x02
STATUS_ACCEL_ALT_ADDR = 0x04

# Little-endian layout after the 2-byte company ID (which bleak strips):
# magic[2] ver status seq tilt sg temp batt ax ay az
PAYLOAD = struct.Struct("<2sBBBHHhHhhh")


def decode(data):
    """Decode manufacturer data (without the company ID) into a dict."""
    if len(data) < PAYLOAD.size or data[:2] != MAGIC:
        return None
    (_, ver, status, seq, tilt, sg, temp, batt, ax, ay, az) = \
        PAYLOAD.unpack_from(data)
    if ver != 1:
        return None
    return {
        "seq": seq,
        "status": status,
        "tilt_deg": tilt / 100.0,
        "sg": sg / 10000.0 if sg else None,
        "temp_c": temp / 100.0,
        "battery_v": batt / 1000.0,
        "accel_mg": (ax, ay, az),
    }


def poly_sg(coeffs, tilt):
    return sum(c * tilt ** i for i, c in enumerate(coeffs))


def describe_status(status):
    flags = []
    if status & STATUS_ACCEL_ERROR:
        flags.append("ACCELEROMETER NOT FOUND")
    if status & STATUS_ACCEL_ALT_ADDR:
        flags.append("accel at alt address 0x18")
    return ", ".join(flags)


async def run(args):
    from bleak import BleakScanner

    last_seq = {}
    writer = None
    if args.csv:
        f = open(args.csv, "a", newline="")
        writer = csv.writer(f)
        if f.tell() == 0:
            writer.writerow(["time", "address", "tilt_deg", "sg", "temp_c",
                             "battery_v", "ax_mg", "ay_mg", "az_mg", "rssi"])

    def on_adv(device, adv):
        data = adv.manufacturer_data.get(COMPANY_ID)
        if data is None:
            return
        r = decode(data)
        if r is None:
            return
        if last_seq.get(device.address) == r["seq"] and not args.all:
            return
        last_seq[device.address] = r["seq"]

        sg = r["sg"]
        if args.poly:
            sg = poly_sg(args.poly, r["tilt_deg"])
        now = datetime.datetime.now().isoformat(timespec="seconds")
        sg_txt = f"{sg:.4f}" if sg is not None else "uncalibrated"
        ax, ay, az = r["accel_mg"]
        line = (f"{now} {device.address} tilt={r['tilt_deg']:6.2f} deg "
                f"SG={sg_txt} temp={r['temp_c']:.2f} C "
                f"batt={r['battery_v']:.2f} V "
                f"accel=({ax},{ay},{az}) mg rssi={adv.rssi}")
        extra = describe_status(r["status"])
        print(line + (f"  [{extra}]" if extra else ""), flush=True)
        if writer:
            writer.writerow([now, device.address, r["tilt_deg"],
                             f"{sg:.4f}" if sg is not None else "",
                             r["temp_c"], r["battery_v"], ax, ay, az,
                             adv.rssi])
            f.flush()

    async with BleakScanner(on_adv):
        print("Listening for brew pills... (Ctrl-C to stop)", file=sys.stderr)
        while True:
            await asyncio.sleep(3600)


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--csv", help="append readings to this CSV file")
    p.add_argument("--poly", type=float, nargs="+", metavar="C",
                   help="SG polynomial coefficients C0 C1 [C2 [C3]]")
    p.add_argument("--all", action="store_true",
                   help="print every advert, not just new readings")
    args = p.parse_args()
    try:
        asyncio.run(run(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
