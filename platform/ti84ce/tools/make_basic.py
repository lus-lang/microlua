#!/usr/bin/env python3
"""make_basic.py - tokenize a TI-BASIC source file into an .8xp program.

The source file is everything after the '--- program source below this
line ---' marker (so the file can carry a prose header). ASCII digraphs are
translated to TI tokens: '->' (store), '<=', '>=', '!=', '^2' (squared).

Usage: make_basic.py <source.txt> <NAME> [out.8xp]
Requires: pip install tivars
"""

import os
import sys

from tivars import TIProgram

MARKER = "--- program source below this line ---"


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 1
    src_path, name = sys.argv[1], sys.argv[2]
    out = sys.argv[3] if len(sys.argv) > 3 else name.upper() + ".8xp"

    text = open(src_path, encoding="utf-8").read()
    if MARKER in text:
        text = text.split(MARKER, 1)[1]
    text = (text.strip("\n")
            .replace("->", "→")   # store arrow
            .replace("<=", "≤")
            .replace(">=", "≥")
            .replace("!=", "≠")
            .replace("~", "⁻")    # negation (distinct from binary minus!)
            .replace("^2", "²"))  # squared
    for n in "123456":            # builtin list names are subscripted
        text = text.replace("L" + n, "L" + "₁₂₃₄₅₆"["123456".index(n)])

    # Some commands only match their token with the canonical trailing
    # space; without it the tokenizer falls back to letter tokens.
    lines = []
    for line in text.split("\n"):
        if line in ("FnOff", "FnOn", "PlotsOff", "PlotsOn", "Pause",
                    "PrintScreen"):
            line += " "
        lines.append(line)
    text = "\n".join(lines)

    prog = TIProgram(name=name.upper())
    prog.load_string(text)

    # Verify: decode back and compare, and reject any line that tokenized
    # into individual letter tokens where a command was clearly meant.
    back = prog.string()
    for orig, dec in zip(text.split("\n"), back.split("\n")):
        if dec.rstrip() != orig.rstrip():
            print(f"tokenization mismatch:\n  in:  {orig!r}\n  out: {dec!r}",
                  file=sys.stderr)
            return 1
    # Lowercase letters are the 2-byte tokens 0xBB 0xB0..0xCA; outside
    # quoted strings they mean a command name failed to match its token
    # (usually a missing canonical trailing space, e.g. "Pause ").
    for line in text.split("\n"):
        if '"' in line:
            continue
        q = TIProgram(name="CHK")
        q.load_string(line)
        d = q.data
        for i in range(len(d) - 1):
            if d[i] == 0xBB and 0xB0 <= d[i + 1] <= 0xCA:
                print(f"lowercase letter tokens in: {line!r} - a command "
                      f"probably needs its canonical trailing space",
                      file=sys.stderr)
                return 1

    prog.save(out)
    print(f"wrote {out} ({os.path.getsize(out)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
