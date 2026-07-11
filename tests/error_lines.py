#!/usr/bin/env python3
"""Runtime-error line reporting: errors must carry the exact source line.

Probes the binary first (line info is a port knob, MLUA_ENABLE_LINEINFO):
- enabled builds must report the exact line of a runtime error both in the
  `[line N]:` prefix and in the `src:N:` traceback lines;
- gated builds must omit the `[line N]:` prefix and print `?` for every
  traceback line number.

Exit codes follow meson's convention: 0 = pass, 1 = fail.
"""

import os
import subprocess
import sys

import _wrap
import tempfile

# The nil-index error is raised on line 12 of this script (1-based).
SCRIPT = """\
local t = {}

local function ok()
  return 1
end

ok()
ok()

local function boom()
  local x
  return x.field
end

boom()
"""
ERROR_LINE = 12


def run(mlua, args):
    return subprocess.run(
        mlua + args,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=30,
        check=False,
    )


def main():
    if len(sys.argv) < 2:
        sys.stderr.write("usage: error_lines.py <mlua>\n")
        return 1
    mlua = _wrap.mlua_cmd(sys.argv[1])

    probe = run(mlua, ["-e", "local x return x.y"])
    if probe.returncode == 0:
        sys.stderr.write("probe: expected an error exit\n")
        return 1
    lineinfo = "[line " in probe.stderr

    with tempfile.NamedTemporaryFile("w", suffix=".lua", delete=False) as f:
        f.write(SCRIPT)
        path = f.name
    try:
        result = run(mlua, [path])
    finally:
        os.unlink(path)

    if result.returncode == 0:
        sys.stderr.write("expected an error exit, got success\n")
        return 1

    if lineinfo:
        # Script-file errors are prefixed "path:N: Error:"; the traceback
        # repeats the exact line ("path:N: in function").
        if f":{ERROR_LINE}: Error:" not in result.stderr:
            sys.stderr.write(f"expected ':{ERROR_LINE}: Error:' in stderr, "
                             f"got {result.stderr!r}\n")
            return 1
        if f":{ERROR_LINE}: " not in result.stderr.split(
                "stack traceback:", 1)[-1]:
            sys.stderr.write(f"expected ':{ERROR_LINE}:' in the traceback, "
                             f"got {result.stderr!r}\n")
            return 1
    else:
        if f":{ERROR_LINE}: Error:" in result.stderr:
            sys.stderr.write("gated build unexpectedly reported a line: "
                             f"{result.stderr!r}\n")
            return 1
        if "stack traceback:" in result.stderr and ":?: " not in result.stderr:
            sys.stderr.write("gated build should print '?' line numbers, got "
                             f"{result.stderr!r}\n")
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
