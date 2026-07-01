#!/usr/bin/env python3
"""Compare exact memory usage for MicroLua and Lua 5.5 on derived workloads."""

import argparse
import glob
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WORKLOAD_DIR = os.path.join(HERE, "workloads")
LUA_PEAK_RUNNER_C = os.path.join(HERE, "lua_peak_runner.c")
LUA_PEAK_RE = re.compile(r"__BENCH_LUA_PEAK__\s+(\d+)")
MLUA_PEAK_RE = re.compile(r"Heap Peak:\s+(\d+) bytes")
MLUA_HEAP_BYTES = 16 * 1024 * 1024


def find_mlua(override):
    candidates = [override] if override else [
        os.path.join(ROOT, "builddir-release", "mlua"),
        os.path.join(ROOT, "builddir", "mlua"),
    ]
    for path in candidates:
        if path and os.path.exists(path):
            return path
    return None


def find_lua(override):
    names = [override] if override else ["lua5.5", "lua55", "lua"]
    for name in names:
        path = name if override and os.path.exists(name) else shutil.which(name)
        if not path:
            continue
        proc = subprocess.run([path, "-v"], capture_output=True, text=True)
        if "Lua 5.5" in (proc.stdout + proc.stderr):
            return path
    return None


def run_capture(cmd):
    proc = subprocess.run(cmd, capture_output=True, text=True)
    return proc.returncode, proc.stdout.strip(), proc.stderr.strip()


def pkg_config(name):
    proc = subprocess.run(["pkg-config", "--cflags", "--libs", name],
                          capture_output=True, text=True)
    if proc.returncode == 0:
        return shlex.split(proc.stdout)
    return None


def build_lua_peak_runner(out):
    cc = shutil.which("cc")
    if not cc:
        raise RuntimeError("cc not found; cannot build Lua peak runner")
    flags = pkg_config("lua5.5") or pkg_config("lua-5.5") or pkg_config("lua")
    if not flags:
        raise RuntimeError("pkg-config could not find Lua 5.5")
    cmd = [cc, LUA_PEAK_RUNNER_C, "-o", out] + flags
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "failed to build Lua peak runner")


def lua_peak_bytes(runner, workload):
    rc, _, err = run_capture([runner, workload])
    if rc != 0:
        raise RuntimeError("Lua peak runner failed for %s: %s" %
                           (os.path.basename(workload), err))
    match = LUA_PEAK_RE.search(err)
    if not match:
        raise RuntimeError("Lua peak marker missing for %s" %
                           os.path.basename(workload))
    return int(match.group(1))


def mlua_peak_bytes(mlua, workload):
    rc, out, err = run_capture([
        mlua, "--memory-limit", str(MLUA_HEAP_BYTES), "-d", workload
    ])
    if rc != 0:
        raise RuntimeError("MicroLua failed for %s: %s" %
                           (os.path.basename(workload), err))
    match = MLUA_PEAK_RE.search(out)
    if not match:
        raise RuntimeError("MicroLua peak marker missing for %s" %
                           os.path.basename(workload))
    return int(match.group(1))


def human_bytes(value):
    if value is None:
        return "n/a"
    if value < 1024:
        return "%d B" % value
    if value < 1024 * 1024:
        return "%.1f KiB" % (value / 1024)
    return "%.2f MiB" % (value / (1024 * 1024))


def fmt_ratio(value):
    if value is None:
        return "n/a"
    return "%.2fx" % value


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlua")
    parser.add_argument("--lua")
    args = parser.parse_args()

    mlua = find_mlua(args.mlua)
    lua = find_lua(args.lua)
    if not mlua:
        print("mlua not found; build builddir-release first", file=sys.stderr)
        return 2
    if not lua:
        print("Lua 5.5 not found; skipping")
        return 0

    focus = {
        "binarytrees": "allocation churn",
        "knucleotide": "substring table pressure",
        "revcomp": "string builder pressure",
        "spectralnorm": "numeric arrays",
    }

    with tempfile.TemporaryDirectory() as td:
        runner = os.path.join(td, "lua_peak_runner")
        try:
            build_lua_peak_runner(runner)
        except RuntimeError as exc:
            print(str(exc), file=sys.stderr)
            return 2

        rows = []
        for workload in sorted(glob.glob(os.path.join(WORKLOAD_DIR, "*.lua"))):
            name = os.path.basename(workload)[:-4]
            rc_lua, out_lua, err_lua = run_capture([lua, workload])
            rc_mlua, out_mlua, err_mlua = run_capture([mlua, workload])
            if rc_lua != 0 or rc_mlua != 0 or out_lua != out_mlua:
                print("mismatch: %s" % name, file=sys.stderr)
                print("lua rc=%s stderr=%s" % (rc_lua, err_lua), file=sys.stderr)
                print("mlua rc=%s stderr=%s" % (rc_mlua, err_mlua), file=sys.stderr)
                return 1

            try:
                lua_peak = lua_peak_bytes(runner, workload)
                mlua_peak = mlua_peak_bytes(mlua, workload)
            except RuntimeError as exc:
                print(str(exc), file=sys.stderr)
                return 1

            rows.append({
                "name": name,
                "lua_peak": lua_peak,
                "mlua_peak": mlua_peak,
                "ratio": (mlua_peak / lua_peak) if lua_peak else None,
            })

    print("| workload | memory-pressure focus | Lua exact peak | MicroLua exact peak | mlua/lua memory |")
    print("|---|---|---:|---:|---:|")
    for row in rows:
        print("| {name} | {focus} | {lua_peak} | {mlua_peak} | {ratio} |".format(
            name=row["name"],
            focus=focus.get(row["name"], ""),
            lua_peak=human_bytes(row["lua_peak"]),
            mlua_peak=human_bytes(row["mlua_peak"]),
            ratio=fmt_ratio(row["ratio"]),
        ))

    for row in rows:
        if row["name"] == "revcomp" and row["mlua_peak"] >= row["lua_peak"]:
            print("revcomp memory regression: MicroLua peak %s >= Lua peak %s" %
                  (human_bytes(row["mlua_peak"]), human_bytes(row["lua_peak"])),
                  file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
