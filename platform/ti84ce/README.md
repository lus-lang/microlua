# MicroLua on the TI-84 Plus CE

Two programs built with the [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain):

| Program | Directory | Contents |
|---|---|---|
| `MLUA.8xp` (~60 KB) | `repl/` | Full build: runs Lua **source or bytecode** appvars, on-calc **REPL** |
| `MLUAR.8xp` (~47 KB) | `runner/` | Bytecode-only runner (no compiler); smallest footprint. Ships **typed float arrays** (`MLUA_TABLE_NUM_ARRAYS`): a table of floats retains ~4 bytes/element instead of ~20, so float-heavy workloads fit the 48 KB heap. The repl build skips this (~2.8 KB of image it can't spare). |

Both include the `gfx` / `key` / `timer` calculator bindings and ship as a
single compressed `.8xp` (zx0; the decompressed image is ~108-141 KB of
eZ80 code).

## Building

Install the CE toolchain (CEdev ≥ v15) and put its `bin/` on `PATH`, then:

```sh
cd platform/ti84ce/repl && make      # -> bin/MLUA.8xp
cd platform/ti84ce/runner && make    # -> bin/MLUAR.8xp
```

The makefiles compile the freestanding MicroLua core from `../../../src`
with `ports/ti84ce.h` (24-bit `int`/pointers, binary32 floats, parser depth
capped for the 4 KB C stack, `MLUA_ENABLE_DUMP=0`).

## Installing / running

Send the `.8xp` to the calculator (it is flagged **archived** — the stored
program and its decompressed image cannot both fit in RAM). The programs
depend on the standard C libraries (**LibLoad, graphx, fileioc** —
[clibs.8xg](https://github.com/CE-Programming/libraries/releases)); install
those once, archived.

TI removed assembly-program execution in OS 5.5+, so on modern OS versions
launch through a shell ([Cesium](https://github.com/mateoconlechuga/cesium))
or [arTIfiCE](https://yvantt.github.io/arTIfiCE/). On OS ≤ 5.4,
`Asm(prgmMLUA` works directly.

## Scripts

Scripts are appvars. The picker lists appvars containing MicroLua bytecode
(magic `\x1BMLu`) — and, in the full build, anything that looks like Lua
source text. On a PC:

```sh
./builddir/mlua -o script.mlu script.lua        # compile (optional)
convbin --iformat bin --oformat 8xv --input script.mlu \
        --output SCRIPT.8xv --name SCRIPT --archive
```

Plain `.lua` source can be converted the same way for the full build, which
compiles it on the calculator. `require("mod")` maps the module name to an
appvar (uppercased, truncated to 8 chars) holding source or bytecode.

In the picker: **enter** runs the selection, **mode** opens the REPL (full
build), **clear** exits. In the REPL: one line = one chunk (locals do not
persist across lines), alpha types lowercase, the OS lowercase-alpha mode
types uppercase, `sto->` types `=`, the TEST menu (2nd+math) gives
comparison operators, and **clear** on an empty line exits.

## Calculator bindings

All light C functions (see `common/bindings_ce.c`):

- `gfx.begin() / gfx.finish()` — enter/leave 320x240 8bpp graphics mode
  (`finish`, because `end` is a Lua keyword). The frontend always restores
  the OS screen after a chunk, even on error.
- `gfx.swap() fill(c) color(c) pixel(x,y) line(x1,y1,x2,y2) rect(x,y,w,h)
  fillRect(...) circle(x,y,r) fillCircle(...) text(s,x,y) textColor(fg[,bg])`
  — thin graphx wrappers; colors are default-palette (xlibc) indices.
- `key.scan()` — scan the keypad; `key.isDown(code)` — test a key
  (constants: `key.up .down .left .right .enter .clear .second .alpha
  .mode .del`); `key.any()`; `key.row(1..7)` raw row bits.
- `timer.millis()` — ms since program start; `timer.sleep(ms)` — busy wait,
  interruptible with clear.

`examples/demo.lua` is a bouncing-ball gfx demo; `examples/smoke.lua` is the
deterministic script used by the automated tests.

## MicroLua vs TI-BASIC benchmarks

`examples/mandel.*` and `examples/bench_*.{lua,basic.txt}` are matched
pairs running identical computations with self-reported timings and
integer-exact checksums (every run below produced the exact reference
value). Measured in CEmu on OS 5.7; each language wins where its
architecture says it should:

| benchmark | workload | TI-BASIC | MicroLua | winner |
|---|---|---|---|---|
| `bench_int` | scalar integer loop, 20k iterations | 161 s | 16.0 s | **MicroLua 10.1x** |
| `mandel` | 32x24 Mandelbrot floats, compute | 121 s | 39.8 s | **MicroLua 3.0x** |
| `mandel` | draw phase (one pixel/cell) | 5 s | 1.3 s | **MicroLua 3.8x** |
| `bench_list` | 60 element-wise passes over 500-element lists | 20 s | 18.8 s | **MicroLua 1.06x** |
| `bench_str` | build 1000-char string by 500 appends + scan | 11 s | 5.2 s | **MicroLua 2.1x** |

MicroLua wins every row. BOTH columns were re-measured 2026-07-03 on the
same CEmu ROM: the TI-BASIC times came back identical to the previous
recordings (the OS interpreter is unchanged, which also validates the
measurement setup), while the MicroLua side improved across the board
from perf pass 4 (no-clear allocator, append fast arm, v7 fused-locals
opcodes). Screens carry exact checksums on both sides; the regression
gate is these values +2%; bytecode is v7 - recompile old .mlu appvars. `bench_list` is TI-BASIC's best case --
`L1L2+L1` is one dispatch into vectorized OS assembly -- and the fused
indexing opcodes (`GETTABLE_LL`/`SETTABLE_LL`), the array-window fast
paths, and computed-goto dispatch bring per-element bytecode to parity
with it. `bench_str` used to lose 9.4x to two quadratic costs that are
gone: `string.byte` decoded UTF-8 from the start of the string on every
call (an all-ASCII flag now makes positions O(1)), and the GC collected
every ~256 bytes allocated once live data sat above the reserve ceiling
(pacing is now geometric in the remaining space).

Rebuild the BASIC side from source with `tools/make_basic.py` (needs
`pip install tivars`); it normalizes ASCII digraphs to TI tokens and
verifies the tokenization. The Lua side runs from source appvars via MLUA.

## Memory budget

- Lua heap: one static 48 KB buffer (`MLUA_CE_HEAP_SIZE`) in the 60 KB
  bss+heap region; a fresh state is created per script run. Tables are the
  hungriest residents (~24 bytes retained per integer element).
- Program image: decompressed into user RAM at launch; the practical
  ceiling is ~137 KB once the VAT and the LibLoad library copies are
  accounted for, though the exact limit depends on what else occupies RAM
  (the pass-4 build launches at 137.2 KB via Cesium in CEmu). The full
  build sits at ~137.2 KB after perf pass 4 (v7 fused-opcode handlers,
  correctness fixes) - `MLUA_ENABLE_PACK=0`, `MLUA_PARSE_FOLD_INT=0` and
  `MLUA_PARSE_FUSE_LOCALS=0` buy the room while `MLUA_VM_COMPUTED_GOTO=1`,
  compare-branch fusion, and the fused-locals HANDLERS stay on, being
  worth more per byte here. Note: on OS 5.5+ the direct `Asm(` launch is
  refused at ANY size (use Cesium/arTIfiCE); only the Cesium autotests
  are a valid launch check on such ROMs. Measure
  with `python3 ../../tools/map_size.py bin/MLUA.map` (image size, largest
  symbols, and `--diff old.map new.map` for per-symbol deltas against a
  saved baseline map).

### Size/perf regression procedure

Before landing a change that touches core code, from `platform/ti84ce/`:

1. Save the current `repl/bin/MLUA.map` and `runner/bin/MLUAR.map` as
   baselines, then rebuild both targets (`make` in each dir).
2. `python3 ../../tools/map_size.py --diff baseline.map bin/MLUA.map` for
   both targets; the image must stay under the ~137 KB ceiling and any
   growth needs a justification in the commit message.
3. Run the CEmu autotester smoke (`autotest.json`, see Tests below) for
   both targets.
4. Re-run the benchmark table above (`bench_int`, `mandel`, `bench_list`,
   `bench_str` from `examples/`) in CEmu, check the printed checksums
   match, and compare the printed `ms` against the table. Timings are
   self-reported by the scripts; every row must be at or below its
   recorded value (allow ~2% CEmu jitter). Update the table when a change
   legitimately shifts a number.
- C stack: ~4 KB. Parser recursion is capped (`MLUA_PARSE_MAX_DEPTH 32`).
  The GC marks iteratively (gray list through the header Forward field), so
  object-graph depth no longer touches the C stack.

## Known limits

- Floats are binary32 (`double` == `float` on eZ80): ~7 significant digits.
- `string.dump` is absent (`MLUA_ENABLE_DUMP=0`), and so are
  `string.pack`/`packsize`/`unpack` (`MLUA_ENABLE_PACK=0`): nothing on the
  calculator consumes packed byte strings, and the format engine costs
  ~7 KB of image.
- Module/appvar names: 8 characters, uppercase.
- The home-screen console is 26x10 with OS scrolling; no scrollback.

## Tests

`autotest.json` in each target dir drives
[CEmu's autotester](https://github.com/CE-Programming/CEmu/tree/master/tests/autotester):

```sh
export AUTOTESTER_ROM=path/to/84ce.rom     # ROMs are not distributable
cemu-autotester autotest.json
```

The default sequences use `Asm(` and need an OS ≤ 5.4 ROM;
`autotest-cesium.json` variants launch through Cesium (APPS slot and list
position are ROM-specific — adjust). The tests transfer the checked-in
`test/*.8xv` fixtures; the standard C libraries must be installed in the
ROM or added to `transfer_files`.
