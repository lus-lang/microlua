#!/usr/bin/env python3
"""
cross_canary.py - compile the freestanding MicroLua core with a cross compiler
and assert the target's fundamental type widths.

This is the "nonstandard target" guardrail: it verifies (never assumes) that the
target has the widths we claim by reading them from the compiler, then confirms
the core actually compiles there. A compile failure on such a target is exactly
the class of bug the width-correct types + static asserts prevent.

Compile-only (no link): bare-metal targets have no hosted libc for the REPL or
the test harness, so we only build the freestanding library translation units.

Exit codes follow meson's convention: 0 = pass, 1 = fail, 77 = skip (the cross
compiler is not installed).

Usage:
  cross_canary.py <cc> <srcroot> <int> <ptr> <double> [extra cc args...]
"""

import os
import shutil
import subprocess
import sys


def preprocessor_widths(cc, extra):
    out = subprocess.run(
        [cc, *extra, "-dM", "-E", "-ffreestanding", "-"],
        input="", capture_output=True, text=True,
    )
    macros = {}
    for line in out.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "#define":
            macros[parts[1]] = parts[2]

    def w(name):
        try:
            return int(macros.get(name, "-1"))
        except ValueError:
            return -1

    return w("__SIZEOF_INT__"), w("__SIZEOF_POINTER__"), w("__SIZEOF_DOUBLE__")


def core_sources(srcroot):
    src = os.path.join(srcroot, "src")
    lib = os.path.join(src, "library")
    files = []
    # Every src/MLua*.c is freestanding except the REPL (libc). Extensions and
    # the REPL are the sanctioned libc users and are excluded.
    for name in sorted(os.listdir(src)):
        if name.startswith("MLua") and name.endswith(".c") and name != "MLuaRepl.c":
            files.append(os.path.join(src, name))
    for name in sorted(os.listdir(lib)):
        if name.endswith(".c"):
            files.append(os.path.join(lib, name))
    return files, src, lib


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 1
    cc = sys.argv[1]
    srcroot = sys.argv[2]
    exp = (int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5]))
    extra = sys.argv[6:]

    if shutil.which(cc) is None:
        print(f"SKIP: {cc} not installed")
        return 77

    got = preprocessor_widths(cc, extra)
    print(f"{cc}: int={got[0]} ptr={got[1]} double={got[2]} "
          f"(expected int={exp[0]} ptr={exp[1]} double={exp[2]})")
    if got != exp:
        print("FAIL: detected type widths do not match the expected target")
        return 1

    files, src, lib = core_sources(srcroot)
    inc = ["-I" + src, "-I" + lib]
    base = [*extra, "-c", "-std=c99", "-ffreestanding",
            "-DMLUA_ENABLE_COMPILER=1", *inc, "-o", os.devnull]
    for f in files:
        r = subprocess.run([cc, *base, f], capture_output=True, text=True)
        if r.returncode != 0:
            print(f"FAIL: {cc} could not compile {os.path.relpath(f, srcroot)}")
            sys.stderr.write(r.stderr[:2000])
            return 1

    print(f"{cc}: all {len(files)} freestanding core TUs compile clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
