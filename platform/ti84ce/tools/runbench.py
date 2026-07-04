"""CEmu screen-capture driver for the on-calc benchmarks.

Runs one benchmark appvar through the CEmu autotester, capturing the VRAM at
each requested delay as stage<N>.rgba/.png next to this script. Used two ways:

  1. Control run: eyeball the stage PNGs to confirm the PASS screen, then read
     the matching CRC from the autotester output of a `hashWait` gate config
     (see *_run.json, whose expected_CRCs are learned this way).
  2. Regression gate: run the *_run.json configs directly with the autotester
     once their expected_CRCs carry the learned values.

Usage: python3 runbench.py lua BENCHINT.8xv 25000[,<more delays>]
       python3 runbench.py prgm AABENCH.8xp 5000

Environment:
  CEMU_AUTOTESTER  path to CEmu's tests/autotester binary (required)
  AUTOTESTER_ROM   path to a TI-84 Plus CE ROM image (required; OS <= 5.4
                   for the direct-launch sequence, or use a Cesium config)

Fixture appvars for the four benchmarks live in fixtures/ (bytecode; must be
regenerated with `mlua -o` after any bytecode version bump). The repl's
`test/clibs/` provides libload/graphx for the `lua` launch path.
"""
import json
import os
import struct
import subprocess
import sys
import zlib

S = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(S, "..", "..", ".."))
kind, name = sys.argv[1], sys.argv[2]
delays = [int(x) for x in sys.argv[3].split(",")]

autotester = os.environ.get("CEMU_AUTOTESTER")
rom = os.environ.get("AUTOTESTER_ROM")
if not autotester or not rom:
    sys.exit("set CEMU_AUTOTESTER and AUTOTESTER_ROM (see module docstring)")

fixture = name if os.path.isabs(name) else os.path.join(S, "fixtures", name)

if kind == "lua":
    seq = (["delay|2000", "key|apps", "delay|1000", "key|4", "delay|2500"]
           + ["key|down"] * 10
           + ["delay|500", "key|enter", "delay|3500", "key|enter"])
    transfers = [
        f"{REPO}/platform/ti84ce/repl/bin/MLUA.8xp",
        fixture,
        f"{REPO}/platform/ti84ce/repl/test/clibs/libload.8xv",
        f"{REPO}/platform/ti84ce/repl/test/clibs/graphx.8xv",
    ]
    target = {"name": "MLUA", "isASM": True}
else:
    seq = ["delay|2000", "key|prgm", "delay|800", "key|1", "delay|800",
           "key|1", "delay|500", "key|enter"]
    transfers = [fixture]
    target = {"name": "AABENCH", "isASM": False}

hashes = {}
for i, d in enumerate(delays, 1):
    seq += [f"delay|{d}", f"hashWait|{i}"]
    hashes[str(i)] = {"description": f"stage{i}", "start": "vram_start",
                      "size": "vram_16_size", "expected_CRCs": ["00000000"],
                      "timeout": 500}

cfg = {"rom": rom, "transfer_files": transfers, "target": target,
       "sequence": seq, "hashes": hashes}
json.dump(cfg, open(f"{S}/run.json", "w"), indent=2)

env = dict(os.environ, AUTOTESTER_ROM=rom)
subprocess.run([autotester, "-s", f"{S}/run.json"],
               capture_output=True, env=env, cwd=S)


def png_chunk(t, d):
    c = t + d
    return struct.pack(">I", len(d)) + c + struct.pack(">I", zlib.crc32(c))


for i in range(1, len(delays) + 1):
    data = open(f"{S}/stage{i}.rgba", "rb").read()[:320 * 240 * 4]
    raw = b"".join(b"\x00" + data[y * 320 * 4:(y + 1) * 320 * 4]
                   for y in range(240))
    png = (b"\x89PNG\r\n\x1a\n"
           + png_chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 240, 8, 6, 0,
                                            0, 0))
           + png_chunk(b"IDAT", zlib.compress(raw, 9))
           + png_chunk(b"IEND", b""))
    open(f"{S}/stage{i}.png", "wb").write(png)
print("done", len(delays), "stages")
