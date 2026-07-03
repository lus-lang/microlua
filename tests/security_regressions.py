#!/usr/bin/env python3

import os
import subprocess
import sys


def run_lua(mlua, source, args=None, timeout=5):
    cmd = [mlua]
    if args:
        cmd.extend(args)
    cmd.extend(["-e", source])
    return subprocess.run(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=timeout,
        check=False,
    )


def require_ok(name, result, expected=None):
    if result.returncode != 0:
        sys.stderr.write(f"{name}: exited {result.returncode}\n")
        sys.stderr.write(result.stderr)
        return 1
    if expected is not None and result.stdout.strip() != expected:
        sys.stderr.write(f"{name}: expected {expected!r}, got {result.stdout!r}\n")
        sys.stderr.write(result.stderr)
        return 1
    return 0


def main(argv):
    if len(argv) != 2:
        print("usage: security_regressions.py MLUA_EXE", file=sys.stderr)
        return 2

    mlua = os.path.abspath(argv[1])

    cases = [
        (
            "unmatched_capture",
            # ".)" forces the pattern engine (bare ")" is a literal per the
            # PUC SPECIALS rule and never reaches the matcher).
            'local ok = pcall(function() string.find("xy", ".)") end); print(ok)',
            None,
            "false",
        ),
        (
            "literal_close_paren_is_plain",
            # Reference behavior: ")" has no magic chars, so it searches as
            # a literal instead of erroring.
            'print(string.find("x)", ")"))',
            None,
            "2\t2",
        ),
        (
            "too_many_captures",
            'local p = string.rep("()", 80); local ok = pcall(function() string.find("x", p) end); print(ok)',
            None,
            "false",
        ),
        (
            "format_precision_clamped",
            'local s = string.format("%.1000f", 1 / 3); print(#s < 80)',
            None,
            "true",
        ),
        (
            "negative_table_ranges",
            'print(table.concat({}, "", 1, -1) == ""); local n=0; for k,v in pairs({table.unpack({}, 1, -1)}) do n=n+1 end; print(n)',
            None,
            "true\n0",
        ),
        (
            "huge_exponent_bounded",
            'local x = tonumber("1e2147483647"); print(type(x))',
            None,
            "number",
        ),
        (
            "sort_comparator_gc",
            'local t={"delta","charlie","bravo","alpha"}; table.sort(t, function(a,b) local s=""; for i=1,200 do s=s.."xxxxxxxx" end; return a < b end); print(t[1], t[4])',
            ["--memory-limit", "65536"],
            "alpha\tdelta",
        ),
        (
            "coroutine_error_survives_gc",
            'local co=coroutine.create(function() error(string.rep("e", 1000)) end); coroutine.resume(co); for i=1,300 do local s=string.rep("x", 1000) end; local ok,msg=coroutine.close(co); print(ok, #msg > 900)',
            ["--memory-limit", "65536"],
            "false\ttrue",
        ),
    ]

    for name, source, args, expected in cases:
        try:
            result = run_lua(mlua, source, args=args)
        except subprocess.TimeoutExpired:
            sys.stderr.write(f"{name}: timed out\n")
            return 1
        failed = require_ok(name, result, expected)
        if failed:
            return failed

    huge_args = "local function f(...) return 1 end; f(" + ",".join(["1"] * 300) + ")"
    result = run_lua(mlua, huge_args)
    if result.returncode == 0 or "expression too complex" not in result.stderr:
        sys.stderr.write("huge_arg_expression: expected expression-too-complex failure\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return 1

    nested_expr = "print(" + ("(" * 600) + "1" + (")" * 600) + ")"
    result = run_lua(mlua, nested_expr)
    if result.returncode == 0 or "source nesting too deep" not in result.stderr:
        sys.stderr.write("nested_expression: expected nesting-depth failure\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return 1

    nested_block = ("do " * 600) + "local x = 1 " + ("end " * 600)
    result = run_lua(mlua, nested_block)
    if result.returncode == 0 or "source nesting too deep" not in result.stderr:
        sys.stderr.write("nested_block: expected nesting-depth failure\n")
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
