#!/usr/bin/env python3
"""get_clibs.py - fetch the LibLoad + graphx appvars the autotests transfer.

Downloads the CE standard-library release, extracts the two libraries the
MicroLua programs depend on into <target>/test/clibs/, and sets their
archived flag (unarchived they would eat the calculator's RAM budget).

Usage: get_clibs.py [destdir]   (default: ../test/clibs relative to script)
"""

import io
import os
import struct
import sys
import urllib.request
import zipfile

URL = ("https://github.com/CE-Programming/libraries/releases/latest/"
       "download/clibs_separately_in_zip.zip")
WANTED = ("libload.8xv", "graphx.8xv")


def set_archived(data):
    data = bytearray(data)
    hdrlen = struct.unpack("<H", data[55:57])[0]
    assert hdrlen == 13, hdrlen
    data[55 + 2 + 2 + 1 + 8 + 1] |= 0x80  # var entry flag byte
    data[-2:] = struct.pack("<H", sum(data[55:-2]) & 0xFFFF)
    return bytes(data)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    dest = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
        here, "..", "test", "clibs")
    os.makedirs(dest, exist_ok=True)
    print("fetching", URL)
    blob = urllib.request.urlopen(URL).read()
    with zipfile.ZipFile(io.BytesIO(blob)) as z:
        for info in z.infolist():
            base = os.path.basename(info.filename)
            if base in WANTED:
                out = os.path.join(dest, base)
                open(out, "wb").write(set_archived(z.read(info)))
                print("wrote", out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
