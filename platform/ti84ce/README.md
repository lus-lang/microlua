# MicroLua on the TI-84 Plus CE

Two programs built with the [CE C/C++ Toolchain](https://github.com/CE-Programming/toolchain):

| Program | Directory | Contents |
|---|---|---|
| `MLUA.8xp` (~59 KB) | `repl/` | Full build: runs Lua **source or bytecode** appvars, on-calc **REPL** |
| `MLUAR.8xp` (~46 KB) | `runner/` | Bytecode-only runner (no compiler); smallest footprint |

Both include the `gfx` / `key` / `timer` calculator bindings and ship as a
single compressed `.8xp` (zx0; the decompressed image is ~110-139 KB of
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

## Memory budget

- Lua heap: one static 40 KB buffer (`MLUA_CE_HEAP_SIZE`) in the 60 KB
  bss+heap region; a fresh state is created per script run.
- Program image: decompressed into user RAM at launch; the full build is
  close to the practical ceiling (~140 KB with the VAT and OS overhead), so
  core additions to the `repl/` target should watch `objdump -h` sizes.
- C stack: ~4 KB. Parser recursion is capped (`MLUA_PARSE_MAX_DEPTH 32`);
  deeply nested table constructors are bounded by GC mark recursion
  (roughly 60-100 levels).

## Known limits

- Floats are binary32 (`double` == `float` on eZ80): ~7 significant digits.
- `string.dump` is absent (`MLUA_ENABLE_DUMP=0`).
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
