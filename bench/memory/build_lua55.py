#!/usr/bin/env python3
"""Download and build Lua 5.5.0 for memory comparisons."""

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
URL = "https://www.lua.org/ftp/lua-5.5.0.tar.gz"
ARCHIVE = os.path.join(HERE, "lua-5.5.0.tar.gz")
SRC = os.path.join(HERE, "lua-5.5.0")


def run(cmd, cwd=None):
    proc = subprocess.run(cmd, cwd=cwd)
    if proc.returncode != 0:
        raise SystemExit(proc.returncode)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default=URL)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()

    if args.force and os.path.isdir(SRC):
        shutil.rmtree(SRC)
    if not os.path.exists(ARCHIVE):
        print("downloading %s" % args.url)
        urllib.request.urlretrieve(args.url, ARCHIVE)
    if not os.path.isdir(SRC):
        with tarfile.open(ARCHIVE, "r:gz") as tf:
            tf.extractall(HERE)

    target = "macosx" if platform.system() == "Darwin" else "linux-readline"
    make = shutil.which("make")
    if not make:
        print("make not found", file=sys.stderr)
        return 2
    flags = "-Oz -fno-stack-protector"
    run([make, target, "MYCFLAGS=%s" % flags], cwd=SRC)
    lua = os.path.join(SRC, "src", "lua")
    if not os.path.exists(lua):
        print("failed to build %s" % lua, file=sys.stderr)
        return 1
    print(lua)
    return 0


if __name__ == "__main__":
    sys.exit(main())
