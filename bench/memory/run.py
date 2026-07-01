#!/usr/bin/env python3
"""Run MicroLua/Lua 5.5 memory diagnostics on shared workloads."""

import argparse
import glob
import json
import os
import platform
import re
import select
import shlex
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
WORKLOAD_DIR = os.path.join(HERE, "workloads")
RESULTS_DIR = os.path.join(HERE, "results")
MLUA_MARKER = re.compile(r"__MLUA_MEMORY_JSON__\s+({.*})")
LUA_MARKER = re.compile(r"__LUA55_MEMORY_JSON__\s+({.*})")


def run(cmd):
    return subprocess.run(cmd, capture_output=True, text=True)


def pkg_config(name):
    proc = run(["pkg-config", "--cflags", "--libs", name])
    if proc.returncode == 0:
        return shlex.split(proc.stdout)
    return None


def compile_cmd(out, src, cflags, libs):
    cc = shutil.which("cc")
    if not cc:
        raise RuntimeError("cc not found")
    cmd = [cc, src, "-o", out] + cflags + libs
    proc = run(cmd)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "compile failed")


def build_lua_runner(lua, out):
    src_dir = os.path.dirname(os.path.abspath(lua))
    lib = os.path.join(src_dir, "liblua.a")
    if os.path.exists(lib):
        flags = ["-I", src_dir]
        libs = [lib, "-lm"]
    else:
        flags = pkg_config("lua5.5") or pkg_config("lua-5.5")
        libs = []
        if not flags:
            flags = ["-I", src_dir]
            libs = ["-llua", "-lm"]
    compile_cmd(out, os.path.join(HERE, "lua55_memory_runner.c"), flags, libs)


def build_mlua_runner(mlua, out):
    build_dir = os.path.dirname(os.path.abspath(mlua))
    lib = os.path.join(build_dir, "libmicrolua.a")
    if not os.path.exists(lib):
        raise RuntimeError("missing %s; build MicroLua first" % lib)
    compile_cmd(out, os.path.join(HERE, "mlua_memory_runner.c"),
                ["-I", os.path.join(ROOT, "src"),
                 "-I", os.path.join(ROOT, "src", "library")],
                [lib, "-lm"])


def rss_command(cmd):
    return None


def run_wait4(cmd):
    out_r, out_w = os.pipe()
    err_r, err_w = os.pipe()
    pid = os.fork()
    if pid == 0:
        os.close(out_r)
        os.close(err_r)
        os.dup2(out_w, 1)
        os.dup2(err_w, 2)
        os.close(out_w)
        os.close(err_w)
        os.execvp(cmd[0], cmd)

    os.close(out_w)
    os.close(err_w)
    chunks = {out_r: [], err_r: []}
    open_fds = [out_r, err_r]
    while open_fds:
        ready, _, _ = select.select(open_fds, [], [])
        for fd in ready:
            data = os.read(fd, 8192)
            if data:
                chunks[fd].append(data)
            else:
                open_fds.remove(fd)
                os.close(fd)
    _, status, usage = os.wait4(pid, 0)
    if os.WIFEXITED(status):
        returncode = os.WEXITSTATUS(status)
    else:
        returncode = 128 + os.WTERMSIG(status)
    rss = usage.ru_maxrss
    if platform.system() != "Darwin":
        rss *= 1024
    return returncode, b"".join(chunks[out_r]).decode(), \
        b"".join(chunks[err_r]).decode(), rss


def run_runner(cmd, marker):
    returncode, stdout, stderr, rss = run_wait4(cmd)
    match = marker.search(stderr)
    if not match:
        if returncode != 0:
            raise RuntimeError(stderr.strip() or "runner failed")
        raise RuntimeError("diagnostic marker missing")
    data = json.loads(match.group(1))
    data["peak_rss"] = rss
    return stdout, data


def fmt_bytes(value):
    if value is None:
        return "n/a"
    if value < 1024:
        return "%d B" % value
    if value < 1024 * 1024:
        return "%.1f KiB" % (value / 1024.0)
    return "%.2f MiB" % (value / (1024.0 * 1024.0))


def ratio(a, b):
    if not b:
        return "n/a"
    return "%.2fx" % (a / b)


def markdown(results):
    lines = []
    lines.append("| workload | Lua peak RSS | MicroLua peak RSS | Lua heap peak | MicroLua heap peak | Lua retained | MicroLua retained | mlua/lua retained |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
    for row in results:
        lua = row["lua"]
        mlua = row["mlua"]
        lua_peak = lua["after_execute"]["heap_peak"]
        mlua_peak = mlua["after_execute"]["heap_peak"]
        lua_ret = lua["after_gc"]["heap_current"]
        mlua_ret = mlua["after_gc"]["heap_used"]
        lines.append("| {name} | {lua_rss} | {mlua_rss} | {lua_peak} | {mlua_peak} | {lua_ret} | {mlua_ret} | {ret_ratio} |".format(
            name=row["name"],
            lua_rss=fmt_bytes(lua.get("peak_rss")),
            mlua_rss=fmt_bytes(mlua.get("peak_rss")),
            lua_peak=fmt_bytes(lua_peak),
            mlua_peak=fmt_bytes(mlua_peak),
            lua_ret=fmt_bytes(lua_ret),
            mlua_ret=fmt_bytes(mlua_ret),
            ret_ratio=ratio(mlua_ret, lua_ret),
        ))
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlua", default=os.path.join(ROOT, "build-memory", "mlua"))
    parser.add_argument("--lua", default=os.path.join(HERE, "lua-5.5.0", "src", "lua"))
    parser.add_argument("--out", default=os.path.join(RESULTS_DIR, "baseline.json"))
    args = parser.parse_args()

    if not os.path.exists(args.mlua):
        print("missing MicroLua binary: %s" % args.mlua, file=sys.stderr)
        return 2
    if not os.path.exists(args.lua):
        print("missing Lua 5.5 binary: %s" % args.lua, file=sys.stderr)
        return 2

    os.makedirs(RESULTS_DIR, exist_ok=True)
    mlua_runner = os.path.join(RESULTS_DIR, "mlua_memory_runner")
    lua_runner = os.path.join(RESULTS_DIR, "lua55_memory_runner")
    try:
        build_mlua_runner(args.mlua, mlua_runner)
        build_lua_runner(args.lua, lua_runner)
    except RuntimeError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    results = []
    for workload in sorted(glob.glob(os.path.join(WORKLOAD_DIR, "*.lua"))):
        name = os.path.basename(workload)[:-4]
        lua_out, lua_data = run_runner([lua_runner, workload], LUA_MARKER)
        mlua_out, mlua_data = run_runner([mlua_runner, workload], MLUA_MARKER)
        if lua_out != mlua_out:
            print("stdout mismatch for %s" % name, file=sys.stderr)
            return 1
        results.append({
            "name": name,
            "workload": workload,
            "lua": lua_data,
            "mlua": mlua_data,
        })

    payload = {
        "mlua": os.path.abspath(args.mlua),
        "lua": os.path.abspath(args.lua),
        "workloads": results,
    }
    with open(args.out, "w") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
        f.write("\n")

    md = markdown(results)
    md_path = os.path.splitext(args.out)[0] + ".md"
    with open(md_path, "w") as f:
        f.write(md)
        f.write("\n")
    print(md)
    print("wrote %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
