#!/usr/bin/env python3

import os
import subprocess
import sys

import _wrap
import tempfile


def main(argv):
    if len(argv) != 2:
        print("usage: cli_bytecode.py MLUA_EXE", file=sys.stderr)
        return 2

    mlua = os.path.abspath(argv[1])
    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "chunk.lua")
        out = os.path.join(td, "chunk.mlu")
        with open(src, "w", encoding="utf-8") as f:
            f.write("print('must not execute')\nreturn 42\n")

        run = subprocess.run(
            [*_wrap.mlua_cmd(mlua), "-o", out, src],
            cwd=td,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        if run.returncode != 0:
            sys.stderr.write(run.stderr.decode("utf-8", "replace"))
            return run.returncode
        if run.stdout:
            sys.stderr.write("-o executed the chunk unexpectedly\n")
            return 1

        with open(out, "rb") as f:
            magic = f.read(4)
        if magic != b"\x1bMLu":
            sys.stderr.write("bytecode output has wrong magic: %r\n" % (magic,))
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
