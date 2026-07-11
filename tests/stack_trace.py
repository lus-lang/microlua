#!/usr/bin/env python3
"""Stack-trace shape regression: a deep-recursion error must produce a
well-formed, NUL-terminated traceback no matter how small the trace buffer.

The trace is built into the static MLUA_STACKTRACE_BUF_SIZE buffer with
clamped writes; a stack-overflow trace (one line per frame, default frame
depth 64) overflows the default 2 KB buffer, so this exercises the clamping
path that size-reducing ports rely on when they shrink the knob.

Exit codes follow meson's convention: 0 = pass, 1 = fail.
"""

import re
import subprocess
import sys

import _wrap

# Line numbers print as digits, or `?` in MLUA_ENABLE_LINEINFO=0 builds.
TRACE_LINE = re.compile(r"^\t.+:(\d+|\?): in (function|main chunk)$")


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: stack_trace.py <mlua>\n")
        return 1
    mlua = _wrap.mlua_cmd(sys.argv[1])

    source = "local function f(n) return 1 + f(n + 1) end f(1)"
    result = subprocess.run(
        [*mlua, "-e", source],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
        check=False,
    )

    if result.returncode == 0:
        sys.stderr.write("expected an error exit, got success with stdout "
                         f"{result.stdout!r}\n")
        return 1
    if "stack overflow" not in result.stderr:
        sys.stderr.write(f"expected 'stack overflow' in stderr, got "
                         f"{result.stderr!r}\n")
        return 1
    if "stack traceback:" not in result.stderr:
        sys.stderr.write(f"expected a traceback in stderr, got "
                         f"{result.stderr!r}\n")
        return 1

    trace = result.stderr.split("stack traceback:", 1)[1]
    lines = [l for l in trace.splitlines() if l]
    if len(lines) < 2:
        sys.stderr.write(f"expected several trace lines, got {trace!r}\n")
        return 1
    # Every complete line must be well-formed; the final line may have been
    # truncated by the buffer clamp, which is the behavior under test.
    for line in lines[:-1]:
        if not TRACE_LINE.match(line):
            sys.stderr.write(f"malformed trace line {line!r}\n")
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
