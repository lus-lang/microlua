#!/usr/bin/env python3
"""Deep-object-graph collection under a small C stack.

The GC marks iteratively (gray list threaded through header Forward fields);
a recursive marker would consume one native stack frame per graph level and
overflow on graphs deeper than the stack. This builds a ~40,000-deep table
chain inside a memory limit tight enough that collections must run while the
chain is live, with the C stack clamped small enough that per-level mark
recursion could not survive.

Exit codes follow meson's convention: 0 = pass, 1 = fail.
"""

import resource
import subprocess
import sys

import _wrap

DEPTH = 40000
STACK_LIMIT = 512 * 1024  # far less than DEPTH * any plausible frame size


def limit_stack():
    soft, hard = resource.getrlimit(resource.RLIMIT_STACK)
    want = STACK_LIMIT if hard == resource.RLIM_INFINITY else min(
        STACK_LIMIT, hard)
    resource.setrlimit(resource.RLIMIT_STACK, (want, hard))


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: gc_deep.py <mlua>\n")
        return 1
    mlua = _wrap.mlua_cmd(sys.argv[1])

    source = (
        f"local t = false "
        f"for i = 1, {DEPTH} do t = {{next = t, v = i}} end "
        f"local n = 0 "
        f"while t do n = n + 1 t = t.next end "
        f"print(n)"
    )
    # ~150 B/link on 64-bit -> ~6 MB live; a 12 MB limit forces collections
    # while the full chain is reachable, so the marker walks all of it.
    result = subprocess.run(
        [*mlua, "--memory-limit", str(12 * 1024 * 1024), "-e", source],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=60,
        preexec_fn=limit_stack,
        check=False,
    )

    if result.returncode != 0:
        sys.stderr.write(f"exited {result.returncode} (signal = crashed "
                         f"marker?)\nstderr: {result.stderr}\n")
        return 1
    if result.stdout.strip() != str(DEPTH):
        sys.stderr.write(f"expected {DEPTH}, got {result.stdout!r}\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
