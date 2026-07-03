#!/usr/bin/env python3
"""
map_size.py - measure and diff MicroLua image sizes.

Two input kinds, auto-detected per file:

  1. A GNU-ld style linker map (e.g. platform/ti84ce/repl/bin/MLUA.map).
     The image size is the end of the highest program-region fragment minus
     the load base (default 0xD1A87F, the CE LOAD_ADDR; override with
     --base). Per-symbol sizes come from -ffunction-sections/-fdata-sections
     fragments (.text.<sym>, .rodata.<sym>, ...).

  2. Anything else (ELF binary, static archive): sizes are read with
     `nm -S`, so per-symbol accounting matches the manual `nm --size-sort`
     check documented in CLAUDE.md.

Usage:
    python3 tools/map_size.py FILE [--top N] [--base 0xADDR]
    python3 tools/map_size.py --diff OLD NEW [--top N] [--base 0xADDR]

`--diff` prints per-symbol deltas (grown, shrunk, added, removed) and the
net change; for maps it also reports the image-size delta against the base.
Exit status is 0 on success, 1 on parse/tool failure.
"""

import argparse
import os
import re
import subprocess
import sys

DEFAULT_BASE = 0xD1A87F  # TI-84 CE LOAD_ADDR (see platform/ti84ce/README.md)

# " .text.MLuaFormat   0x00d1b2f4   0x1381 obj/MLUA.o" (input fragment) or
# ".text   0x00d1b2f4   0x1381" (output section). Long section names wrap:
# the name appears alone and addr/size follow on the next line.
FRAG_RE = re.compile(
    r"^\s*(\.[A-Za-z0-9_.$]+)?\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)(?:\s+(\S+))?\s*$"
)
NAME_ONLY_RE = re.compile(r"^\s*(\.[A-Za-z0-9_.$]+)\s*$")

# Output sections whose fragments count toward the loaded image. .bss lives
# in a separate memory region on the CE and never counts.
IMAGE_SECTIONS = ("text", "rodata", "data", "init", "header")


def looks_like_map(path):
    try:
        with open(path, "rb") as f:
            head = f.read(65536)
    except OSError as e:
        sys.exit("error: cannot read %s: %s" % (path, e))
    if b"\0" in head:
        return False
    return b"Linker script and memory map" in head or b"Memory Configuration" in head


def parse_map(path):
    """Return (symbols: {name: size}, image_end: int) from a GNU-ld map.

    Only fragments in the program image (address-descending toward the load
    base is not assumed; we filter by section kind and take max end address).
    Fragment names like .text._MLuaFormat map to symbol _MLuaFormat; plain
    .text/.rodata fragments (string pools etc.) are aggregated per object.
    """
    symbols = {}
    image_end = 0
    in_map = False
    pending_name = None
    with open(path, "r", errors="replace") as f:
        for line in f:
            if not in_map:
                if line.startswith("Linker script and memory map"):
                    in_map = True
                continue
            line = line.rstrip("\n")
            if not line.strip():
                pending_name = None
                continue
            m = NAME_ONLY_RE.match(line)
            if m:
                pending_name = m.group(1)
                continue
            m = FRAG_RE.match(line)
            if not m:
                pending_name = None
                continue
            name, addr_s, size_s, obj = m.groups()
            if name is None:
                name = pending_name
            pending_name = None
            if name is None or obj is None:
                # Output-section summary lines / symbol-definition lines
                # (0xADDR _symbol): sizes come from fragments, skip.
                continue
            addr, size = int(addr_s, 16), int(size_s, 16)
            if size == 0:
                continue
            parts = name.lstrip(".").split(".", 1)
            kind = parts[0]
            if kind not in IMAGE_SECTIONS:
                continue
            image_end = max(image_end, addr + size)
            if len(parts) == 2 and parts[1]:
                sym = parts[1]
            else:
                sym = "[%s in %s]" % (kind, os.path.basename(obj))
            symbols[sym] = symbols.get(sym, 0) + size
    if not symbols:
        sys.exit("error: no image fragments found in %s (not a linker map?)" % path)
    return symbols, image_end


def parse_nm(path):
    """Return {name: size} for an ELF/archive via nm -S."""
    try:
        out = subprocess.run(
            ["nm", "-S", "--size-sort", path],
            capture_output=True, text=True, check=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit("error: nm failed on %s: %s" % (path, e))
    symbols = {}
    for line in out.splitlines():
        fields = line.split()
        # ADDR SIZE TYPE NAME (archives interleave "member.o:" headers/blank)
        if len(fields) != 4:
            continue
        _addr, size_s, _type, name = fields
        try:
            size = int(size_s, 16)
        except ValueError:
            continue
        symbols[name] = symbols.get(name, 0) + size
    if not symbols:
        sys.exit("error: nm found no sized symbols in %s" % path)
    return symbols, None


def load(path):
    return parse_map(path) if looks_like_map(path) else parse_nm(path)


def fmt_size(n):
    return "%d B (%.1f KB)" % (n, n / 1024.0)


def report_single(path, base, top):
    symbols, image_end = load(path)
    total = sum(symbols.values())
    print("%s" % path)
    if image_end:
        print("  image size: %s  (end 0x%X - base 0x%X)"
              % (fmt_size(image_end - base), image_end, base))
    print("  accounted symbol bytes: %s in %d symbols" % (fmt_size(total), len(symbols)))
    print("  top %d symbols:" % top)
    for name, size in sorted(symbols.items(), key=lambda kv: -kv[1])[:top]:
        print("    %8d  %s" % (size, name))


def report_diff(old_path, new_path, base, top):
    old, old_end = load(old_path)
    new, new_end = load(new_path)
    deltas = []
    for name in set(old) | set(new):
        d = new.get(name, 0) - old.get(name, 0)
        if d:
            deltas.append((d, name, old.get(name), new.get(name)))
    deltas.sort(key=lambda t: t[0])
    net = sum(d for d, _, _, _ in deltas)
    print("diff: %s -> %s" % (old_path, new_path))
    if old_end and new_end:
        print("  image size: %s -> %s  (delta %+d B)"
              % (fmt_size(old_end - base), fmt_size(new_end - base),
                 (new_end - old_end)))
    print("  net symbol delta: %+d B across %d changed symbols" % (net, len(deltas)))
    shrunk = [t for t in deltas if t[0] < 0][:top]
    grown = [t for t in deltas if t[0] > 0][-top:][::-1]
    if shrunk:
        print("  shrunk/removed:")
        for d, name, o, n in shrunk:
            print("    %+8d  %-40s %s -> %s" % (d, name, o if o is not None else "-",
                                                n if n is not None else "gone"))
    if grown:
        print("  grown/added:")
        for d, name, o, n in grown:
            print("    %+8d  %-40s %s -> %s" % (d, name, o if o is not None else "new",
                                                n if n is not None else "-"))
    if not deltas:
        print("  no symbol-size changes (zero-delta)")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("files", nargs="+",
                    help="one file to report, or two with --diff")
    ap.add_argument("--diff", action="store_true",
                    help="diff two inputs (OLD NEW)")
    ap.add_argument("--base", type=lambda s: int(s, 0), default=DEFAULT_BASE,
                    help="image load base for maps (default 0x%X)" % DEFAULT_BASE)
    ap.add_argument("--top", type=int, default=25,
                    help="how many symbols to list (default 25)")
    args = ap.parse_args()
    if args.diff:
        if len(args.files) != 2:
            ap.error("--diff needs exactly two files")
        report_diff(args.files[0], args.files[1], args.base, args.top)
    else:
        if len(args.files) != 1:
            ap.error("expected one file (or use --diff OLD NEW)")
        report_single(args.files[0], args.base, args.top)
    return 0


if __name__ == "__main__":
    sys.exit(main())
