#!/usr/bin/env python3
"""embed_chunk.py - emit a C header embedding a file as a byte array.

Usage: embed_chunk.py <input> <ArrayName> > out.h
"""

import os
import sys


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        return 1
    path, name = sys.argv[1], sys.argv[2]
    data = open(path, "rb").read()
    guard = "MLUA_CE_" + name.upper() + "_H"
    print("/*")
    print(f" * Generated from {os.path.basename(path)} by embed_chunk.py.")
    print(" */")
    print()
    print(f"#ifndef {guard}")
    print(f"#define {guard}")
    print()
    print(f"static const unsigned char {name}[] = {{")
    for i in range(0, len(data), 12):
        row = ", ".join(f"0x{b:02X}" for b in data[i:i + 12])
        print(f"    {row},")
    print("};")
    print()
    print(f"#endif /* {guard} */")
    return 0


if __name__ == "__main__":
    sys.exit(main())
