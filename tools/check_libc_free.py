#!/usr/bin/env python3
"""
check_libc_free.py - enforce MicroLua's "the core is libc-free" invariant.

Two independent checks:

  1. Source-include scan (always): no core source under src/ may
     #include a hosted libc header, except:
       - src/MLuaRepl.c and src/extensions/**  (the sanctioned libc users), and
       - includes guarded by  #ifdef MLUA_DEBUG  /  #ifdef MLUA_GC_TRACE
     Only the C99 *freestanding* headers are allowed bare.

  2. Symbol scan (when a static library path is given): the release
     libmicrolua.a must not pull in any libc symbol. The only acceptable
     undefined symbols are libm (sin/cos/pow/... from __builtin lowering),
     the project's own Mem*/Str* freestanding replacements, and internal
     MLua* cross-object references.

Usage:
    python3 tools/check_libc_free.py [LIBMICROLUA_A]

Exit status is non-zero if any violation is found. Matches the manual `nm`
check documented in CLAUDE.md, turned executable.
"""

import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC_DIR = os.path.join(REPO_ROOT, "src")

# C99/C11 freestanding headers: guaranteed present even with no hosted libc.
FREESTANDING_HEADERS = {
    "stdarg.h", "stdbool.h", "stddef.h", "stdint.h",
    "float.h", "iso646.h", "limits.h", "stdalign.h", "stdnoreturn.h",
}

# Core files allowed to use libc (the embedder-facing edge of the runtime).
def is_libc_allowed_path(relpath):
    relpath = relpath.replace(os.sep, "/")
    return relpath == "src/MLuaRepl.c" or relpath.startswith("src/extensions/")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*<([^>]+)>')
# A conditional whose condition mentions one of these gates a debug/trace-only
# region where libc is acceptable.
GUARD_RE = re.compile(r'^\s*#\s*if(?:def|ndef)?\b.*\bMLUA_(?:DEBUG|GC_TRACE)\b')
COND_OPEN_RE = re.compile(r'^\s*#\s*if(?:def|ndef)?\b')
COND_CLOSE_RE = re.compile(r'^\s*#\s*endif\b')


def scan_includes():
    """Return a list of violation strings (empty == clean)."""
    violations = []
    for dirpath, _dirs, files in os.walk(SRC_DIR):
        for name in sorted(files):
            if not (name.endswith(".c") or name.endswith(".h")):
                continue
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, REPO_ROOT)
            if is_libc_allowed_path(rel):
                continue

            # Track preprocessor conditional nesting. Each stack entry is True
            # if that conditional (or any enclosing one) is a debug/trace guard,
            # meaning libc includes inside it are acceptable.
            guard_stack = []
            with open(full, "r", encoding="utf-8", errors="replace") as fh:
                for lineno, line in enumerate(fh, start=1):
                    if COND_OPEN_RE.match(line):
                        inside_guard = bool(guard_stack and guard_stack[-1])
                        guard_stack.append(inside_guard or bool(GUARD_RE.match(line)))
                        continue
                    if COND_CLOSE_RE.match(line):
                        if guard_stack:
                            guard_stack.pop()
                        continue
                    m = INCLUDE_RE.match(line)
                    if not m:
                        continue
                    header = m.group(1).strip()
                    if header in FREESTANDING_HEADERS:
                        continue
                    if guard_stack and guard_stack[-1]:
                        continue  # inside a MLUA_DEBUG / MLUA_GC_TRACE region
                    violations.append(
                        "%s:%d: hosted libc include <%s> in libc-free core"
                        % (rel, lineno, header)
                    )
    return violations


# Undefined symbols that are acceptable in the freestanding static library.
LIBM = {
    "acos", "acosf", "asin", "asinf", "atan", "atanf", "atan2", "atan2f",
    "cbrt", "ceil", "ceilf", "copysign", "cos", "cosf", "cosh", "exp",
    "exp2", "expf", "fabs", "fabsf", "floor", "floorf", "fmax", "fmin",
    "fmod", "fmodf", "frexp", "hypot", "ldexp", "log", "log10", "log2",
    "logf", "modf", "modff", "nan", "pow", "powf", "round", "roundf",
    "scalbn", "sin", "sinf", "sinh", "sqrt", "sqrtf", "tan", "tanf",
    "tanh", "trunc", "truncf",
}
ALLOWED_PREFIXES = ("MLua", "Mem", "Str")

UNDEF_RE = re.compile(r'^\s+[Uu]\s+(\S+)$')


def normalize(sym):
    # Mach-O prefixes symbols with a single leading underscore.
    return sym[1:] if sym.startswith("_") else sym


def is_allowed_symbol(sym):
    s = normalize(sym)
    if s in LIBM:
        return True
    return s.startswith(ALLOWED_PREFIXES)


def run_nm(lib_path):
    for argv in (["nm", lib_path], ["xcrun", "nm", lib_path]):
        try:
            return subprocess.run(
                argv, capture_output=True, text=True, check=False
            ).stdout
        except FileNotFoundError:
            continue
    raise RuntimeError("could not run `nm` (not found, and `xcrun nm` failed)")


def scan_symbols(lib_path):
    """Return a list of violation strings (empty == clean)."""
    out = run_nm(lib_path)
    bad = set()
    for line in out.splitlines():
        m = UNDEF_RE.match(line)
        if not m:
            continue
        sym = m.group(1)
        if not is_allowed_symbol(sym):
            bad.add(sym)
    return ["undefined libc symbol %s in %s" % (s, os.path.basename(lib_path))
            for s in sorted(bad)]


def main(argv):
    lib_path = None
    for a in argv[1:]:
        if a in ("-h", "--help"):
            print(__doc__)
            return 0
        if a.startswith("-"):
            sys.stderr.write("unknown option: %s\n" % a)
            return 2
        lib_path = a

    violations = []

    inc = scan_includes()
    violations.extend(inc)
    print("[includes] scanned core src/ -> %d violation(s)" % len(inc))

    if lib_path is not None:
        if not os.path.exists(lib_path):
            sys.stderr.write("error: static library not found: %s\n" % lib_path)
            return 2
        sym = scan_symbols(lib_path)
        violations.extend(sym)
        print("[symbols]  scanned %s -> %d violation(s)"
              % (os.path.basename(lib_path), len(sym)))
    else:
        print("[symbols]  skipped (no static library path given)")

    if violations:
        sys.stderr.write("\nlibc-free invariant VIOLATED:\n")
        for v in violations:
            sys.stderr.write("  - %s\n" % v)
        return 1

    print("OK: core is libc-free")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
