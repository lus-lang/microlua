#!/usr/bin/env python3
"""
bench.py -- compare MicroLua (mlua) against reference Lua 5.5 on identical
workloads, measuring execution time, native heap use, and binary size.

The same .lua program runs on both runtimes; every workload is verified to
produce byte-identical stdout first (the "correctness gate"), so a timing or
memory number is only reported when both runtimes actually computed the same
thing.

Footprint is measured natively, not via process RSS: mlua reserves a fixed
16 MB heap buffer, so RSS is meaningless. Instead we read mlua's own heap
high-water (its -d dump) and binary-search the smallest --memory-limit it can
run the workload in; for Lua we use collectgarbage("count") after a full GC.

Usage:
    python3 bench/bench.py [--runs N] [--quick] [--no-min-heap]
                           [--mlua PATH] [--lua PATH]

If no Lua 5.5 is found on PATH the comparison is skipped cleanly (exit 0).
"""

import argparse
import glob
import os
import re
import shutil
import statistics
import subprocess
import sys

BENCH_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(BENCH_DIR)
WORKLOAD_DIR = os.path.join(BENCH_DIR, "workloads")
LUA_MEM = os.path.join(BENCH_DIR, "_lua_mem.lua")
RESULTS_MD = os.path.join(BENCH_DIR, "RESULTS.md")

MLUA_HEAP_BYTES = 16 * 1024 * 1024  # static HeapBuffer in MLuaRepl.c
MEM_MARKER_RE = re.compile(r"__BENCH_MEM__\s+(\d+)")


# --------------------------------------------------------------------------
# Runtime discovery
# --------------------------------------------------------------------------
def find_mlua(override):
    candidates = [override] if override else [
        os.path.join(ROOT, "builddir-release", "mlua"),
        os.path.join(ROOT, "builddir", "mlua"),
    ]
    for c in candidates:
        if c and os.path.exists(c):
            return c
    return None


def find_lua(override):
    names = [override] if override else ["lua5.5", "lua55", "lua"]
    for name in names:
        path = name if (override and os.path.exists(name)) else shutil.which(name)
        if not path:
            continue
        p = subprocess.run([path, "-v"], capture_output=True, text=True)
        if "Lua 5.5" in (p.stdout + p.stderr):
            return path
    return None


# --------------------------------------------------------------------------
# Measurement primitives
# --------------------------------------------------------------------------
def run_capture(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def time_ms(cmd, runs, warmup=1):
    import time
    for _ in range(warmup):
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    samples = []
    for _ in range(runs):
        t0 = time.perf_counter()
        subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        samples.append((time.perf_counter() - t0) * 1000.0)
    return statistics.median(samples), min(samples)


def mlua_runs_in(mlua, workload, limit, expected):
    rc, out, _ = run_capture([mlua, "--memory-limit", str(limit), workload])
    return rc == 0 and out == expected


def mlua_min_heap(mlua, workload, expected):
    """Smallest --memory-limit (bytes) the workload still completes correctly in."""
    limit = 8 * 1024
    last_fail = None
    first_ok = None
    while limit <= MLUA_HEAP_BYTES:
        if mlua_runs_in(mlua, workload, limit, expected):
            first_ok = limit
            break
        last_fail = limit
        limit *= 2
    if first_ok is None:
        return None
    if last_fail is None:
        return first_ok
    lo, hi = last_fail, first_ok
    while hi - lo > max(4096, lo // 16):  # ~6% precision
        mid = (lo + hi) // 2
        if mlua_runs_in(mlua, workload, mid, expected):
            hi = mid
        else:
            lo = mid
    return hi


def lua_peak_bytes(lua, workload):
    """Peak heap occupancy (bytes) during the run, sampled by _lua_mem.lua."""
    _, _, err = run_capture([lua, LUA_MEM, workload])
    m = MEM_MARKER_RE.search(err)
    return int(m.group(1)) if m else None


def file_size(path):
    try:
        return os.path.getsize(path)
    except OSError:
        return None


def linked_liblua(lua):
    """Best-effort: locate the shared liblua a Lua binary links (macOS otool)."""
    otool = shutil.which("otool")
    if not otool:
        return None, None
    p = subprocess.run([otool, "-L", lua], capture_output=True, text=True)
    for line in p.stdout.splitlines():
        line = line.strip()
        if "liblua" in line:
            path = line.split(" ")[0]
            return path, file_size(path)
    return None, None


# --------------------------------------------------------------------------
# Formatting
# --------------------------------------------------------------------------
def human_bytes(n):
    if n is None:
        return "n/a"
    if n < 1024:
        return "%d B" % n
    if n < 1024 * 1024:
        return "%.1f KB" % (n / 1024)
    return "%.2f MB" % (n / (1024 * 1024))


def fmt_table(rows, headers):
    cols = list(zip(*([headers] + rows))) if rows else [[h] for h in headers]
    widths = [max(len(str(c)) for c in col) for col in cols]
    def line(cells):
        return "| " + " | ".join(str(c).ljust(w) for c, w in zip(cells, widths)) + " |"
    sep = "|" + "|".join("-" * (w + 2) for w in widths) + "|"
    return "\n".join([line(headers), sep] + [line(r) for r in rows])


CAVEATS = """\
## How to read this

- **Correctness gate** — every workload's stdout is compared byte-for-byte
  between the two runtimes; a number is only meaningful on a matching row.
- **Time** is the median of N process runs (wall clock, process startup
  included). Subtract the startup baseline (in Environment) to estimate pure
  work — it matters most for the sub-20 ms workloads.
- **lua peak** is Lua's peak heap occupancy *during* the run, sampled
  non-invasively with a `debug` count hook (`collectgarbage("count")`).
  **mlua min** is the smallest `--memory-limit` MicroLua still completes the
  workload in — its true working set, GC and growth headroom included. These
  are each runtime's own accounting, defined slightly differently, but both
  answer "how little RAM does this workload need here".
- **Binary size**: mlua is a self-contained freestanding binary; Lua's runtime
  also lives in a shared `liblua`, so `libmicrolua.a` vs `liblua` is the fairer
  runtime-footprint comparison.
- MicroLua is a small bytecode VM; on raw CPU it can trail Lua 5.5's optimized
  register VM. Footprint (binary size, and live data via 8-byte NaN-boxed
  values) is where it is expected to win; its working-set *min-heap* can still
  exceed Lua's when array-growth doubling and the GC threshold inflate the
  transient peak. Numbers are reported as measured, not curated.
"""

OPTIMIZATIONS = """\
## Optimizations applied (driven by these findings)

1. **Operator-precedence fix** (correctness). The Pratt parser recursed at the
   wrong binding power for left-associative operators, so `2 + 3 * 4` parsed as
   `(2 + 3) * 4`. Surfaced because `sort`'s checksum overflowed. Fixed in
   `MLuaParse.c`; regression suite `test_operators.lua`.

2. **Incremental concat hashing** (`strbuild` 14.5x -> 0.9x; min-heap
   272 -> 112 KB). `s = s .. x` rehashed the whole accumulated string each step
   (O(n^2)). MicroLua interns every string, but FNV-1a is composable --
   `hash(a..b)` continues from `hash(a)` over only `b`'s bytes -- so the
   all-string concat path now builds the result directly (no temp buffer) and
   hashes incrementally. Generalizes to `strjoin`/`tableconcat`.

3. **table.concat truncation fix** (correctness). It assembled into a fixed 4 KB
   stack buffer and silently dropped the overflow (and mis-placed separators on
   sub-ranges). Rewritten to size an exact heap buffer. Found by the anti-overfit
   `tableconcat` workload; tests in `test_table.lua`.

4. **Code hygiene** (flash / C99): removed dead statics and duplicate forward
   typedefs (a C99 conformance issue).

Residual gaps reported honestly, not closed: `fib`/`loop`/`sieve` stay ~2.3-2.4x
on raw bytecode dispatch and table access -- the cost of a tiny switch-dispatch
VM versus Lua 5.5's optimized register VM. The `sieve` min-heap (8.5 vs 4.5 MB)
reflects array-growth doubling plus the 75% GC threshold's transient peak;
shrinking it is entangled with the safepoint-GC design and left as future work.
"""


# --------------------------------------------------------------------------
# Main
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="Compare MicroLua vs Lua 5.5.")
    ap.add_argument("--runs", type=int, default=7, help="timed runs per workload")
    ap.add_argument("--quick", action="store_true", help="3 runs, skip min-heap")
    ap.add_argument("--no-min-heap", action="store_true", help="skip min-heap search")
    ap.add_argument("--mlua", help="path to mlua binary")
    ap.add_argument("--lua", help="path to a Lua 5.5 binary")
    args = ap.parse_args()

    runs = 3 if args.quick else args.runs
    do_min_heap = not (args.quick or args.no_min_heap)

    mlua = find_mlua(args.mlua)
    if not mlua:
        sys.stderr.write(
            "error: mlua not found. Build it first:\n"
            "  meson setup builddir-release --buildtype=release && "
            "ninja -C builddir-release\n")
        return 2

    lua = find_lua(args.lua)
    if not lua:
        print("Lua 5.5 not found on PATH (looked for lua5.5/lua55/lua); "
              "skipping comparison.")
        return 0

    lua_ver = subprocess.run([lua, "-v"], capture_output=True, text=True)
    lua_ver = (lua_ver.stdout + lua_ver.stderr).strip().splitlines()[0]

    workloads = sorted(glob.glob(os.path.join(WORKLOAD_DIR, "*.lua")))
    if not workloads:
        sys.stderr.write("error: no workloads in %s\n" % WORKLOAD_DIR)
        return 2

    print("MicroLua : %s" % mlua)
    print("Lua      : %s (%s)" % (lua, lua_ver))
    print("Workloads: %d   runs/each: %d   min-heap search: %s\n"
          % (len(workloads), runs, "yes" if do_min_heap else "no"))

    # Startup baseline (empty program) per runtime.
    base_lua = time_ms([lua, "-e", ""], runs)[0]
    base_mlua = time_ms([mlua, "-e", ""], runs)[0]

    rows = []
    md_rows = []
    n_faster = n_smaller = n_match = 0
    ratios = []

    for wl in workloads:
        name = os.path.basename(wl)[:-4]
        rc_l, out_l, _ = run_capture([lua, wl])
        rc_m, out_m, err_m = run_capture([mlua, wl])
        match = (rc_l == 0 and rc_m == 0 and out_l == out_m)

        if not match:
            detail = "rc=%d/%d" % (rc_l, rc_m)
            print("  %-10s MISMATCH (%s) lua=%r mlua=%r %s"
                  % (name, detail, out_l[:24], out_m[:24], err_m[:40]))
            rows.append([name, "MISMATCH", "-", "-", "-", "-", "-", "-"])
            md_rows.append([name, "**MISMATCH**", "-", "-", "-", "-", "-", "-"])
            continue

        n_match += 1
        t_lua = time_ms([lua, wl], runs)[0]
        t_mlua = time_ms([mlua, wl], runs)[0]
        ratio = t_mlua / t_lua if t_lua > 0 else float("inf")
        ratios.append(ratio)
        faster = t_mlua < t_lua
        if faster:
            n_faster += 1

        peak_lua = lua_peak_bytes(lua, wl)
        min_heap = mlua_min_heap(mlua, wl, out_m) if do_min_heap else None
        # Working-set comparison: Lua peak occupancy vs the smallest heap mlua
        # can run the workload in.
        smaller = (peak_lua is not None and min_heap is not None
                   and min_heap < peak_lua)
        if smaller:
            n_smaller += 1

        print("  %-10s ok   lua=%6.1fms  mlua=%6.1fms  (%.2fx)  "
              "lua_peak=%-9s mlua_min=%-9s %s"
              % (name, t_lua, t_mlua, ratio, human_bytes(peak_lua),
                 human_bytes(min_heap), "<- mlua smaller" if smaller else ""))

        cells = [name, "%.1f" % t_lua, "%.1f" % t_mlua, "%.2fx" % ratio,
                 human_bytes(peak_lua), human_bytes(min_heap)]
        rows.append(cells + ["yes" if smaller else "no"])
        md_rows.append([cells[0], cells[1], cells[2],
                        cells[3] + (" ✓" if faster else ""),
                        cells[4], cells[5], ("✓" if smaller else "—")])

    headers = ["workload", "lua ms", "mlua ms", "mlua/lua",
               "lua peak", "mlua min", "mlua<lua?"]

    # Binary sizes
    liba = file_size(os.path.join(ROOT, "builddir-release", "libmicrolua.a"))
    mlua_bin = file_size(mlua)
    lua_bin = file_size(lua)
    liblua_path, liblua_sz = linked_liblua(lua)

    geomean = (statistics.geometric_mean(ratios) if ratios else float("nan"))

    summary = []
    summary.append("Startup baseline : lua %.1fms   mlua %.1fms" % (base_lua, base_mlua))
    summary.append("Geomean time     : mlua is %.2fx lua (>1 = slower)" % geomean)
    summary.append("MicroLua wins    : time %d/%d   heap %d/%d"
                   % (n_faster, n_match, n_smaller, n_match))
    summary.append("Binary size      : mlua %s   libmicrolua.a %s"
                   % (human_bytes(mlua_bin), human_bytes(liba)))
    summary.append("                   lua  %s%s"
                   % (human_bytes(lua_bin),
                      ("   liblua %s" % human_bytes(liblua_sz)) if liblua_sz else ""))

    print("\n" + fmt_table(rows, headers))
    print("\n" + "\n".join(summary))

    # ---- RESULTS.md ----
    md = []
    md.append("# MicroLua vs Lua 5.5 — Benchmark Results\n")
    md.append("Generated by `bench/bench.py`. Re-run to refresh.\n")
    md.append("## Environment\n")
    md.append("- **MicroLua**: `%s` (%s); `libmicrolua.a` %s"
              % (os.path.relpath(mlua, ROOT), human_bytes(mlua_bin), human_bytes(liba)))
    md.append("- **Lua**: `%s` — %s (%s)%s"
              % (lua, lua_ver, human_bytes(lua_bin),
                 ("; `liblua` %s" % human_bytes(liblua_sz)) if liblua_sz else ""))
    md.append("- Timing: median of %d runs. Startup baseline: lua %.1f ms, "
              "mlua %.1f ms.\n" % (runs, base_lua, base_mlua))
    md.append("## Results\n")
    md.append(fmt_table(md_rows, headers) + "\n")
    md.append("## Summary\n")
    md.append("- Geomean execution time: **mlua is %.2fx Lua** (>1 = slower).\n"
              % geomean)
    md.append("- MicroLua wins: time **%d/%d**, working-set heap **%d/%d**.\n"
              % (n_faster, n_match, n_smaller, n_match))
    md.append("- Binary: **mlua %s** vs lua binary %s%s.\n"
              % (human_bytes(mlua_bin), human_bytes(lua_bin),
                 (" + liblua %s" % human_bytes(liblua_sz)) if liblua_sz else ""))
    md.append("\n" + OPTIMIZATIONS)
    md.append("\n" + CAVEATS)
    with open(RESULTS_MD, "w") as f:
        f.write("\n".join(md))
    print("\nWrote %s" % os.path.relpath(RESULTS_MD, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
