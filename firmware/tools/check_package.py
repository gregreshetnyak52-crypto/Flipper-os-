#!/usr/bin/env python3
"""Check that a FlipperOS update package really contains what it should.

    check_package.py UPDATE.tgz [--pack base.tgz] [--pack extra.tgz] [--own APP ...]

A firmware that builds fine can still ship without the community apps
(Unleashed adds them in a separate step), and nobody notices until a
function is missing on the device. So this opens resources.ths inside the
update package and verifies that every file of each given all-the-plugins
pack is there, at the path the Flipper looks for it, and that our own apps
are present. Needs `pip install heatshrink2`.
"""

import argparse
import io
import struct
import sys
import tarfile

import heatshrink2

MAGIC = 0x53445348  # "HSDS": the header written by scripts/update.py
WINDOW, LOOKAHEAD = 13, 6

# Where each folder of an all-the-plugins pack lands on the SD card
PACK_LAYOUT = (
    ("base_pack_build/artifacts-base/", "apps/"),
    ("base_pack_build/apps_data/", "apps_data/"),
    ("extra_pack_build/artifacts-extra/", "apps/"),
)


def package_files(update_tgz):
    """File names inside resources.ths of an update package."""
    with tarfile.open(update_tgz) as update:
        member = next(m for m in update.getmembers() if m.name.endswith("/resources.ths"))
        data = update.extractfile(member).read()

    magic, version, window, lookahead = struct.unpack("<IBBB", data[:7])
    if magic != MAGIC or version != 1 or (window, lookahead) != (WINDOW, LOOKAHEAD):
        sys.exit(f"resources.ths has an unexpected header: {data[:7].hex()}")
    raw = heatshrink2.decompress(data[7:], window_sz2=window, lookahead_sz2=lookahead)
    with tarfile.open(fileobj=io.BytesIO(raw)) as resources:
        return {m.name.lstrip("./") for m in resources.getmembers() if m.isfile()}


def pack_files(pack_tgz):
    """Files of an all-the-plugins pack, as paths on the SD card."""
    expected = set()
    with tarfile.open(pack_tgz) as pack:
        for member in pack.getmembers():
            if not member.isfile():
                continue
            for source, target in PACK_LAYOUT:
                if member.name.startswith(source):
                    expected.add(target + member.name[len(source):])
                    break
            else:
                sys.exit(f"{pack_tgz}: unexpected file {member.name}")
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("update", help="flipper-z-f7-update-*.tgz")
    parser.add_argument("--pack", action="append", default=[], help="all-the-apps-*.tgz that must be inside")
    parser.add_argument(
        "--own", action="append", help="app that must be in Tools/ (default: flipper_os and patrol)"
    )
    args = parser.parse_args()
    args.own = args.own if args.own is not None else ["flipper_os", "patrol"]

    present = package_files(args.update)
    failed = False

    for pack in args.pack:
        expected = pack_files(pack)
        missing = sorted(expected - present)
        print(f"{pack}: {len(expected) - len(missing)}/{len(expected)} files in the package")
        for path in missing[:20]:
            print(f"  missing: {path}")
        failed |= bool(missing)

    for app in args.own:
        path = f"apps/Tools/{app}.fap"
        found = path in present
        print(f"{path}: {'ok' if found else 'MISSING'}")
        failed |= not found

    apps = sum(1 for p in present if p.startswith("apps/") and p.endswith(".fap"))
    print(f"{args.update}: {len(present)} resource files, {apps} apps")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
