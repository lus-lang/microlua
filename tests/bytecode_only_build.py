#!/usr/bin/env python3

import os
import shutil
import subprocess
import sys
import tempfile


def run(cmd, **kwargs):
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
        **kwargs,
    )


def main(argv):
    if len(argv) != 3:
        print("usage: bytecode_only_build.py SOURCE_ROOT MLUA_EXE", file=sys.stderr)
        return 2

    source_root = os.path.abspath(argv[1])
    compiler_mlua = os.path.abspath(argv[2])

    with tempfile.TemporaryDirectory() as td:
        src = os.path.join(td, "chunk.lua")
        bytecode = os.path.join(td, "chunk.mlu")
        with open(src, "w", encoding="utf-8") as f:
            f.write("print(6 * 7)\n")

        dumped = run([compiler_mlua, "-o", bytecode, src], cwd=td)
        if dumped.returncode != 0:
            sys.stderr.write(dumped.stderr.decode("utf-8", "replace"))
            return dumped.returncode

        builddir = os.path.join(td, "build-bytecode")
        setup = run(["meson", "setup", builddir, source_root, "-Dcompiler=false"])
        if setup.returncode != 0:
            sys.stderr.write(setup.stdout.decode("utf-8", "replace"))
            sys.stderr.write(setup.stderr.decode("utf-8", "replace"))
            return setup.returncode

        build = run(["ninja", "-C", builddir])
        if build.returncode != 0:
            sys.stderr.write(build.stdout.decode("utf-8", "replace"))
            sys.stderr.write(build.stderr.decode("utf-8", "replace"))
            return build.returncode

        mlua = os.path.join(builddir, "mlua")
        lib = os.path.join(builddir, "libmicrolua.a")

        bytecode_run = run([mlua, bytecode], cwd=td)
        if bytecode_run.returncode != 0:
            sys.stderr.write(bytecode_run.stderr.decode("utf-8", "replace"))
            return bytecode_run.returncode
        if bytecode_run.stdout.decode("utf-8", "replace") != "42\n":
            sys.stderr.write("bytecode-only runtime produced wrong output\n")
            return 1

        source_run = run([mlua, src], cwd=td)
        if source_run.returncode == 0 or b"source compiler disabled" not in source_run.stderr:
            sys.stderr.write("bytecode-only runtime accepted source input\n")
            return 1

        eval_run = run([mlua, "-e", "print(1)"], cwd=td)
        if eval_run.returncode == 0 or b"compiler-enabled" not in eval_run.stderr:
            sys.stderr.write("bytecode-only runtime accepted -e\n")
            return 1

        nm = shutil.which("nm")
        if nm:
            symbols = run([nm, lib])
            out = symbols.stdout + symbols.stderr
            if b"MLuaParse" in out or b"MLuaLex" in out:
                sys.stderr.write("parser/lexer symbols found in bytecode-only lib\n")
                return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
