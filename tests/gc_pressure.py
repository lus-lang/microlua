#!/usr/bin/env python3
"""GC pressure regressions: workloads in constrained heaps that only pass
when collections keep being scheduled near the top of the heap.

Regression guarded: MLuaNextGCThreshold used to return HeapSize once live
data crossed the growth clamp (~57% of the heap). From then on no collection
was ever requested early, so the allocation that eventually hit the heap
wall failed its operation - with the heap full of collectable garbage.
The threshold now stays an allocation reserve below the wall.

Exit codes follow meson's convention: 0 = pass, 1 = fail.
"""

import subprocess
import sys


def run_lua(mlua, source, limit, timeout=30):
    return subprocess.run(
        [mlua, "--memory-limit", str(limit), "-e", source],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )


def require(name, result, expected):
    if result.returncode != 0:
        sys.stderr.write(f"{name}: exited {result.returncode}\n")
        sys.stderr.write(result.stderr)
        return 1
    if result.stdout.strip() != expected:
        sys.stderr.write(f"{name}: expected {expected!r}, "
                         f"got {result.stdout!r}\n")
        return 1
    return 0


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: gc_pressure.py <mlua>\n")
        return 1
    mlua = sys.argv[1]
    failed = 0

    # Live data above the old saturation point (20 DISTINCT ~2 KB strings;
    # identical ones would be interned into a single object), then churn of
    # heap-allocated temporaries. Only survives if collections keep firing
    # once live data sits above ~57% of the heap.
    churn_above_saturation = (
        "local keep = {} "
        "for i = 1, 20 do keep[i] = string.rep('x', 2000) .. i end "
        "local n = 0 "
        "for i = 1, 2000 do "
        "  local d = 'yyyyyyyyyy' .. i "
        "  n = n + #d "
        "end "
        "print('done', #keep, #keep[20], n)"
    )
    failed += require(
        "churn_above_saturation",
        run_lua(mlua, churn_above_saturation, 65536),
        "done\t20\t2002\t26893",
    )

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
