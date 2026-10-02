#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Makes a tiny PS1 or PS2 disc image for tests and screenshots.

Only what the disc scanner reads: an ISO 9660 volume descriptor and a root
directory with SYSTEM.CNF, whose boot line carries the serial. No game.

  make_test_disc.py OUT.iso --ps2 --serial SLUS_209.46 [--size-mb 12]
  make_test_disc.py OUT.bin --ps1 --serial SCES_014.20   (raw 2352-byte sectors)
"""
import argparse
import struct


def record(extent, size, name, is_dir):
    name_bytes = name.encode("ascii") if isinstance(name, str) else name
    length = 33 + len(name_bytes) + (1 if len(name_bytes) % 2 == 0 else 0)
    rec = bytearray(length)
    rec[0] = length
    rec[2:6] = struct.pack("<I", extent)
    rec[6:10] = struct.pack(">I", extent)
    rec[10:14] = struct.pack("<I", size)
    rec[14:18] = struct.pack(">I", size)
    rec[25] = 2 if is_dir else 0
    rec[32] = len(name_bytes)
    rec[33:33 + len(name_bytes)] = name_bytes
    return bytes(rec)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("out")
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--ps1", action="store_true")
    group.add_argument("--ps2", action="store_true")
    parser.add_argument("--serial", required=True, help="as on the disc, e.g. SLUS_209.46")
    parser.add_argument("--size-mb", type=float, default=0, help="pad the image to this size")
    args = parser.parse_args()

    if args.ps2:
        cnf = f"BOOT2 = cdrom0:\\{args.serial};1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"
    else:
        cnf = f"BOOT = cdrom:\\{args.serial};1\r\nTCB = 4\r\nEVENT = 10\r\nSTACK = 801FFFF0\r\n"
    cnf = cnf.encode("ascii")

    sectors = [bytearray(2048) for _ in range(20)]
    pvd = sectors[16]
    pvd[0] = 1
    pvd[1:6] = b"CD001"
    pvd[6] = 1
    pvd[8:40] = b"PLAYSTATION".ljust(32)
    pvd[156:156 + 34] = record(18, 2048, b"\x00", True)
    sectors[17][0] = 255
    sectors[17][1:6] = b"CD001"
    root = record(18, 2048, b"\x00", True) + record(18, 2048, b"\x01", True) \
        + record(19, len(cnf), "SYSTEM.CNF;1", False)
    sectors[18][:len(root)] = root
    sectors[19][:len(cnf)] = cnf

    raw = args.ps1 and args.out.lower().endswith(".bin")
    with open(args.out, "wb") as f:
        for s in sectors:
            if raw:
                header = bytearray(24)
                header[0] = 0
                header[1:11] = b"\xff" * 10
                header[15] = 2
                f.write(bytes(header) + bytes(s) + bytes(280))
            else:
                f.write(bytes(s))
        target = int(args.size_mb * 1024 * 1024)
        if target > f.tell():
            f.truncate(target)


if __name__ == "__main__":
    main()
