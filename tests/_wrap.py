"""Exe-wrapper support for the CLI test harnesses.

Under a cross build with an exe wrapper (e.g. cross/mips-be-qemu.ini running
big-endian MIPS through qemu-user), the built mlua is a foreign-architecture
binary these scripts cannot exec directly. Meson exports MESON_EXE_WRAPPER to
script tests in that situation; mlua_cmd() turns the mlua path into the full
argv prefix, and harnesses that must configure a nested NATIVE build (which
would not match the cross configuration) skip when wrapped().
"""

import os
import shlex


def mlua_cmd(path):
    wrapper = os.environ.get("MESON_EXE_WRAPPER")
    if wrapper:
        return [*shlex.split(wrapper), path]
    return [path]


def wrapped():
    return bool(os.environ.get("MESON_EXE_WRAPPER"))
