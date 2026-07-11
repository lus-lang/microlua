#!/usr/bin/env python3
"""Pins the MLUA_VM_FUSED_LOCALS_OPS load-time fence.

A chunk compiled with locals fusion (containing OP_GETLOCAL2/OP_ADD_SET)
must be rejected deterministically at LOAD by a build whose handlers are
compiled out -- never hit an unknown opcode mid-run -- while a fusion-free
chunk still loads and runs there. Mirrors bytecode_only_build.py: sets up
a nested bytecode-only build with the handler knob off.
"""

import os
import shutil
import subprocess
import sys

import _wrap
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
    if _wrap.wrapped():
        # Cross run through an exe wrapper: the nested `meson setup` below
        # would configure a NATIVE build that cannot match the cross
        # configuration under test. Covered by the native suites instead.
        sys.stderr.write("skipped: exe-wrapper cross run\n")
        return 77

    if len(argv) != 3:
        print("usage: fused_ops_fence.py SOURCE_ROOT MLUA_EXE", file=sys.stderr)
        return 2

    source_root = os.path.abspath(argv[1])
    compiler_mlua = os.path.abspath(argv[2])

    with tempfile.TemporaryDirectory() as td:
        fused_src = os.path.join(td, "fused.lua")
        fused_bc = os.path.join(td, "fused.mlu")
        plain_src = os.path.join(td, "plain.lua")
        plain_bc = os.path.join(td, "plain.mlu")

        # `s = s + i` inside a two-local body emits ADD_SET (and the pair
        # read emits GETLOCAL2) when the compiling build fuses locals.
        with open(fused_src, "w", encoding="utf-8") as f:
            f.write("local s = 1\nlocal i = 41\ns = s + i\nprint(s)\n")
        # No adjacent local pair, no accumulator: never fused.
        with open(plain_src, "w", encoding="utf-8") as f:
            f.write("print(6 * 7)\n")

        for src, bc in ((fused_src, fused_bc), (plain_src, plain_bc)):
            r = run([compiler_mlua, "-o", bc, src], cwd=td)
            if r.returncode != 0:
                sys.stderr.write(r.stderr.decode("utf-8", "replace"))
                return r.returncode

        fused_bytes = open(fused_bc, "rb").read()
        if 0x81 not in fused_bytes and 0x82 not in fused_bytes:
            # The compiling build has fusion disabled: the fence cannot be
            # exercised from here; treat as skipped (nothing to verify).
            print("compiler emits no fused opcodes; fence not exercisable")
            return 77

        builddir = os.path.join(td, "build-nofusedops")
        setup = run([
            "meson", "setup", builddir, source_root, "-Dcompiler=false",
            "-Dc_args=-DMLUA_VM_FUSED_LOCALS_OPS=0",
        ])
        if setup.returncode != 0:
            sys.stderr.write(setup.stdout.decode("utf-8", "replace"))
            sys.stderr.write(setup.stderr.decode("utf-8", "replace"))
            return setup.returncode
        build = run(["ninja", "-C", builddir, "mlua"])
        if build.returncode != 0:
            sys.stderr.write(build.stdout.decode("utf-8", "replace"))
            return build.returncode

        nofused_mlua = os.path.join(builddir, "mlua")

        rejected = run([nofused_mlua, fused_bc])
        err = rejected.stderr.decode("utf-8", "replace")
        if rejected.returncode == 0 or "fused-locals" not in err:
            sys.stderr.write(
                "expected deterministic fence rejection, got rc=%d\n%s%s"
                % (rejected.returncode,
                   rejected.stdout.decode("utf-8", "replace"), err))
            return 1

        accepted = run([nofused_mlua, plain_bc])
        if (accepted.returncode != 0
                or accepted.stdout.decode("utf-8", "replace").strip() != "42"):
            sys.stderr.write(
                "fusion-free chunk should run, got rc=%d\n%s%s"
                % (accepted.returncode,
                   accepted.stdout.decode("utf-8", "replace"),
                   accepted.stderr.decode("utf-8", "replace")))
            return 1

        shutil.rmtree(builddir, ignore_errors=True)

    print("fence rejects fused chunks; fusion-free chunks run")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
