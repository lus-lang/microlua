# MicroLua — Development Guide

MicroLua (`mlua`) is a tiny, freestanding Lua runtime in C99: NaN-boxed values
on 64-bit, alignment-tagged values with boxed full-width integers on 32-bit, a
1–2 byte stack-machine ISA, single-pass Pratt parser→bytecode, and a Lisp-2
mark-compact GC.
`README.md` is the product spec.

## Build & run

```sh
meson setup builddir                                   # debug: libc allowed, MLUA_DEBUG output
meson setup builddir-release --buildtype=release       # freestanding: -ffreestanding -fno-builtin
meson setup build-bytecode -Dcompiler=false            # bytecode-only: no lexer/parser
ninja -C builddir
./builddir/mlua script.lua                             # run a script
./builddir/mlua -o script.mlu script.lua               # compile source to bytecode
./build-bytecode/mlua script.mlu                       # run precompiled bytecode
./builddir/mlua                                        # interactive REPL
```

- Port/build knobs live in `src/MLuaConfig.h`. Use Meson `-Dport=generic64`,
  `generic32`, `cortex-m`, `riscv32`, or `ti84ce` for built-in presets, or
  `-Dport_header=path/to/header.h` to supply one board-specific header. That
  header may override pointer size, alignment, default stack/frame sizes, GC
  threshold, fixed-width type source, native float subtype/width, math hooks,
  `MLUA_PARSE_MAX_DEPTH`, `MLUA_ENABLE_COMPILER`, and `MLUA_ENABLE_DUMP`.
- `platform/ti84ce/` is a complete board port (TI-84 Plus CE, eZ80: 24-bit
  `int`/pointers, binary32 `double`), built with the external CE C toolchain
  makefiles — not meson. Its `repl/` target is close to the calculator's RAM
  ceiling; watch image size when adding core code. CE-side tests run via
  CEmu's autotester (see `platform/ti84ce/README.md`); the `canary_ez80`
  guard test compiles every core TU with `ez80-clang` when CEdev is
  installed.
- Source compilation is controlled by Meson `-Dcompiler=true|false`. When false,
  `libmicrolua.a` must not contain `MLuaLex`/`MLuaParse` symbols and callers must
  use `MLuaLoadBytecode` / `MLuaDoBytecode` or `MLuaLoadBuffer` / `MLuaDoBuffer`
  with a bytecode buffer. `-o`, `-e`, the REPL, `load`, and `loadfile` are
  compiler-enabled workflows only.
- MicroLua bytecode uses fixed-width serialized fields and an explicit
  endianness byte. It is portable across supported endian/pointer-size targets
  with a compatible MicroLua bytecode version. Numeric constants are serialized
  as canonical IEEE-754 binary64 values and narrowed/widened at the boundary
  when `MLUA_FLOAT_BITS` is 32; unsupported headers must be rejected
  deterministically by `MLuaUndump`.
- The static library (`libmicrolua.a`) must stay libc-free; only the REPL
  (`src/MLuaRepl.c`) and optional extensions may use libc. The only acceptable
  undefined symbols are libm functions (sin/cos/pow/...) from `__builtin_*`
  lowering — embedded toolchains provide those — plus the project's own
  Mem*/StrLen (defined in MLuaCore.o). This is **enforced automatically** by
  `tools/check_libc_free.py` (a source-include scan + an `nm` symbol scan),
  wired into meson as the `guard` suite: `libc_free_includes` runs in every
  build, `libc_free_symbols` runs in the freestanding (non-debug) build. Run it
  by hand with `python3 tools/check_libc_free.py builddir-release/libmicrolua.a`.
- The interactive REPL evaluates **one line = one chunk**: locals do not persist across
  lines. Multi-line snippets must be run from a file or written on a single line.

## Tests

```sh
meson test -C builddir            # EVERYTHING: internal C tests + interpreter suites
meson test -C builddir --suite interpreter   # just the Lua suites
meson test -C builddir --suite smoke         # standalone smoke scripts
cd tests/interpreter && ../../builddir/mlua test_base.lua  # one suite by hand
```

- Interpreter suites end with `assert(test.run())`, so failures exit non-zero;
  `require("_base")` resolves relative to the **cwd** (meson sets the workdir).
- Prefer **interpreter tests** (`tests/interpreter/test_*.lua`, harness in `_base.lua`:
  `describe`/`it`/`expect`) over internal C tests; use C tests only for what Lua can't
  reach (GC compaction details, C API edge cases). `tests/internal/TestCoro.c` runs
  Lua workloads in small heaps to force real collections — extend it for GC work.
- The `guard` suite enforces the libc-free invariant (see Build & run). The symbol
  scan only registers in the freestanding build, so run `meson test -C builddir-release`
  to exercise it fully.
- Smoke scripts live in `tests/smoke/` and are intentionally plain Lua programs:
  Meson only checks that they execute successfully. Use interpreter harness tests
  for assertions and semantic coverage.

## Benchmarks

`python3 bench/bench.py` compares MicroLua against reference **Lua 5.5** on the same
workloads (`bench/workloads/*.lua`), reporting execution time, native heap, and binary
size into `bench/RESULTS.md`. It auto-detects a local `lua5.5`/`lua` (verified `5.5` via
`-v`) and **skips cleanly** if none is found. It is a standalone tool, NOT part of
`meson test`. Notes:

- Workloads must produce **byte-identical stdout** on both runtimes (the harness's
  correctness gate); keep them integer-only with every intermediate `< 2^31` (MicroLua
  ints are 32-bit and **wrap** on overflow), and avoid `^`/float printing, `%` on
  negatives (C-truncated here vs Lua floored), and recursion deeper than ~60 frames.
- Memory is native, not RSS (mlua reserves a fixed 16 MB `HeapBuffer`): Lua peak via a
  `debug` count-hook, MicroLua via a `--memory-limit` min-heap search.
- `--quick` (3 runs, no min-heap) for fast iteration; `--runs N` otherwise.

## Debugging

- Crashes/hangs: use **`debug.py [--timeout N] [exe [args...]]`** (an LLDB wrapper that
  interrupts after a timeout and prints a backtrace). Do **not** invoke LLDB directly,
  and do not "simplify" a failing test to pinpoint a crash — debug the real thing and
  work until it is fully fixed.
- GC issues: compile with `-DMLUA_GC_TRACE` for per-phase traces +
  `MLuaGCVerifyHeap()`; `tests/internal/SmallHeapRunner.c` runs any script in a small
  constrained heap (`small_heap_runner script.lua [heapKB]`) so collections actually
  fire.
- The interactive REPL is line-per-chunk; multi-line repros must go in a file.

## Hard rules (non-negotiable)

1. **Every ISA instruction is 1 or 2 bytes.** A 2-byte instruction is opcode + one `u8`
   operand (≤255). Anything larger must be **pushed on the operand stack** by prior
   instructions. Never create a 3+ byte instruction.
2. **No "simplified"/placeholder implementations.** Implement features fully, in order,
   even when the first item is the most complex. If something is genuinely large, create
   a task/plan boundary — don't land a stub.
3. **Never alter existing tests to mask a bug.** Fixes must not change existing language
   behavior.
4. **Every functional change needs test coverage** — find the covering test or add one
   (interpreter tests preferred).
5. **MicroLua is not vanilla Lua.** We care about language semantics within a tiny
   footprint, not fidelity to the PUC-Rio implementation. Don't assume standard Lua
   internals exist or should exist here.

## Architecture map

| File(s) | Role |
|---|---|
| `src/MLuaCore.c/.h` | Freestanding libc replacements (MemCpy/StrLen/…), math builtins |
| `src/MLuaConfig.h`, `src/ports/` | Central port/build configuration and default embedded presets |
| `src/MLuaValue.c/.h` | Value representation: NaN-boxing (64-bit) / alignment tags plus boxed integers and heap floats (32-bit); GC headers |
| `src/MLuaAlloc.c/.h` | `MLuaState`, bump-pointer heap, constrained/vector state creation, `MLuaGCRef` |
| `src/MLuaGC.c/.h` | Lisp-2 mark-compact GC: mark → compute addresses → update refs → compact |
| `src/MLuaLex.c/.h` | Pull lexer, UTF-8-aware, zero-copy tokens |
| `src/MLuaParse.c` | Single-pass Pratt parser; emits bytecode directly (no AST); jump backpatching |
| `src/MLuaCode.c/.h` | Opcode definitions (1–2 bytes), emission helpers, `MLuaProto`, `MLuaFuncState` |
| `src/MLuaDump.c/.h`, `src/MLuaUndump.c/.h` | Portable bytecode serialization/deserialization |
| `src/MLuaVM.c/.h` | Dispatch loop, calls/frames, stack ops, embedding API (`MLuaSetOutput`/`MLuaSetRequirer`) |
| `src/MLuaFunc.c/.h` | Closures, upvalues (open/closed lifecycle), light C functions |
| `src/MLuaThread.c/.h` | Coroutines |
| `src/MLuaString.c/.h` | Interned strings (FNV-1a hash table; pointer equality = value equality) |
| `src/MLuaUTF8.c/.h` | UTF-8/WTF-8 validate/decode/encode, codepoint offsets |
| `src/MLuaConvert.c/.h` | number↔string conversion without libc |
| `src/MLuaTable.c/.h` | Tables: array part (no holes — runtime error) + hash part + `Forward` delegation |
| `src/library/` | Stdlib: Base, Math, String (incl. pattern engine), Table, Coro |
| `src/MLuaRepl.c` | REPL/CLI; provides output + requirer callbacks; the only libc user |

## Invariants that bite

- **The GC moves objects — but only at safepoints.** Allocations never collect; they
  set `GCPending`, and the dispatch loop collects between instructions (reloading
  frame registers afterwards). C library code therefore never sees objects move
  mid-operation. C code that holds `MLuaValue`s across a call back into Lua
  (`MLuaCall`/`MLuaPCall`) must re-read them from its Args window afterwards or hold
  them via `MLuaGCRef` — a collection may have happened inside.
- **Every heap allocation is header-prefixed** (`OBJTYPE_RAW` for `MLuaAlloc`
  payloads) and MOVABLE. If you add a struct that owns a raw buffer, you must (1)
  mark it in `MLuaGCMarkObject` and (2) remap the pointer in `UpdateReferences` —
  see the TABLE/PROTO/THREAD cases in `MLuaGC.c`.
- Saved PCs are byte OFFSETS, functions are GC values; `RELOAD_FRAME` re-derives
  raw pointers. Never store a raw `MLuaProto*`/code pointer across a safepoint.
- Strings are interned (equal contents ⇒ same value) and the intern table is WEAK
  with `MLUA_FALSE` tombstones. Never mutate string bytes in place.
- `MLuaStringData` on short strings (`MLUA_SHORTSTR_MAX`: 5 bytes on 64-bit,
  3 bytes on 32-bit) returns one of 4 rotating static buffers — a pointer is
  valid until the 4th subsequent call.
- No metatables anywhere — table delegation goes through the table's `Forward` field
  (`table.forward`). `rawget`/`rawset`/`rawequal`/`setmetatable` must not be added.
- All C functions are **light**: no upvalues, registered via `MLuaRegisterCFunc` /
  `MLuaRegisterLib`. (This is why `coroutine.wrap` is intentionally absent.)
- C functions receive arguments via `MLuaGetArg(L, i)` (0-based) /
  `MLuaGetStack(L, i)` (1-based), push results with `MLuaPush`, and return the
  result count (or -1 with `L->ErrorMsg` set for errors).
- Table arrays reject holes: writing beyond `len+1` is a runtime error by design.
- `print`/`require` only exist when the embedder installs callbacks
  (`MLuaSetOutput`/`MLuaSetRequirer`); io/os/dofile/loadfile live in the optional
  libc extension (`src/extensions/MLuaStdLib.c`) — the REPL provides all of it.

## Deliberately NOT done (rejected with reasons — don't "fix" these)

- Keyword lookup / `FindLocal` are linear scans: at ≤23 keywords and typical
  local counts, a hash or binary search costs more code than it saves time.
- `MLuaGCStep` performs a full collection: incremental GC isn't promised by the
  README and the safepoint full-collect is simple and bounded by tiny heaps.
- Pattern matching is byte-based (UTF-8-safe but classes/positions are bytes):
  codepoint-aware classes need Unicode tables that don't fit the footprint.
- `upper`/`lower` are ASCII-only: same reason.
- `return f(g())` / `return f(...)` (CALLM-form tails) are NOT tail calls; only
  plain `OP_CALL` returns are flipped to `OP_TAILCALL`. Correctness is identical,
  only frame reuse differs, and the flip-time check stays trivially safe.
- Parser-level caching of repeated global reads (`string.byte` in a loop):
  `_G` is mutable and a single-pass parser cannot prove safety. The user-side
  idiom is `local byte = string.byte`.
- Skipping the intern-table dedup for concat results: breaks the
  pointer-equality-is-value-equality string invariant. The concat path already
  hashes incrementally and reuses the dedup probe's insert slot.
- Cross-TU error-string deduplication (a central `extern const char` message
  table): measured on the linked CE image, LTO already merges identical
  literals across TUs and every message appears exactly once — the "×15 out
  of memory" grep counts are within-TU repeats every compiler merges. A dedup
  header would churn ~200 call sites for zero bytes.
- `MLUA_IDX_T U16` on the ti84ce port: 16-bit frame/proto fields save ~480 B
  of arena RAM but GROW the eZ80 image ~430 B (masking against the 24-bit
  native word, +52 B of it inside the RunVM hot loop). The knob exists for
  targets with cheap 16-bit loads; the measurement lives in `ports/ti84ce.h`.
- EvalTop register caching in RunVM: attempted-and-dropped per its pre-agreed
  criterion in the 2026-07 perf pass — after the locals/constants pointer
  caching and the compare fast paths landed, the projected gain fell under
  the 3% bar while the sync-point audit (every helper call, safepoint, yield,
  and error unwind) remained the riskiest change on the table.
- `MLUA_INT_BITS 16` (narrower runtime integer): collides with the portable
  bytecode format's fixed 32-bit integer constants (`BC_INT_SIZE 4`); a
  16-bit-int port could not load ordinary chunks deterministically.
- Whole-subsystem fences for UTF-8 / coroutine core / pattern engine
  (`MLUA_ENABLE_UTF8`-style): deferred, not refused — UTF-8 handling is woven
  through the lexer and string internals, and the per-library knobs already
  let a port drop the entire string library (patterns and format included),
  which captures most of the image win without invasive fencing. A dedicated
  pass with its own build matrix is the right vehicle. The same applies to an
  integer-only (float-free) build, the single biggest lever for a 6502-class
  port.
- Parser shrink-to-fit of proto buffers: the trim's transient peak (old and
  new buffer both live) is exactly wrong for the CE repl's 48 KB on-calc
  compiles; the ~50-150 B/proto slack only matters on hosts that don't care.
- Freestanding decimal float parsing is a final-ulp off on some literals
  (e.g. `0.25` parses one ulp high, so `0.5 + 0.25 ~= 0.75`): known,
  pre-existing divergence from reference Lua; an exact strtod needs big-int
  machinery that doesn't fit the footprint. Tests use exact binary fractions
  (`1/4`) instead of decimal literals where it matters.

## Status (2026-07-03, follow-up perf/footprint pass landed)

All supported build-variant suites are green locally: debug, freestanding
release, generic32, bytecode-only, true 32-bit (`cross/x86-multilib.ini`),
the narrow-config variants, and the new knob variants (folding/fusion off,
shift-xor hashing, 32-bit formatting). Bytecode is at v6: the dead
OP_GLOOP_SETUP slot was retired and five fused opcodes appended
(SETGLOBAL_K, GETFIELD_K, SETFIELD_K, SETFIELD_K_POP, SELF_K) plus four
compare+branch opcodes (JMPF_EQ/NEQ/LT/LE); older .mlu chunks must be
recompiled and the runner's SMOKE.8xv fixture was regenerated.

Host bench (bench/bench.py vs Lua 5.5): geomean 3.24x, from 7.05x at the
pass baseline; `sort` (0.94x) and `tableconcat` (0.76x) now beat reference
Lua outright, and `sieve` fell from 149x to 3.5x (table array growth was
quadratic above 1024 slots — now x1.5 geometric, costing some min-heap
headroom on array-heavy workloads by design). Five correctness bugs were
fixed en route, each with pinned regressions: hash-part deletion orphaned
probe chains (dead-node scheme now), the compactor broke address-hashed
table keys (GCFLAG_HASHSTALE + rehash-on-access), table.sort could silently
drop ranges from its fixed stack (bounded push-larger scheme now, plus
median-of-3/Hoare for the quadratic inputs), `..` dropped float operands
outright and corrupted INT_MIN, and on the NaN-boxing path a hardware NaN
(0xFFF8...) aliased MLUA_NIL (MakeDouble canonicalizes NaNs now). Pattern
escapes of magic characters (`%.`, `%%`) also never matched — fixed.

Parser features are knob-gated for image-tight ports (`MLUA_PARSE_FOLD_INT`
off on the CE repl, `MLUA_PARSE_FUSE_COMPARE` on everywhere); the fused-op
VM handlers are always compiled so any v6 runtime runs any v6 chunk.
New 8/16-bit enabler knobs (defaults preserve current behavior):
print/format buffer sizes, per-library MLUA_ENABLE_*LIB, RAM floors
(MLUA_MIN_HEAP_SIZE, MLUA_DEFAULT_LOCALS_SIZE, GC pacing floors),
MLUA_HASH_SHIFT_XOR, MLUA_FORMAT_INT64 (0 on the CE: -417 B/target), and
the inline-int/short-string limits now derive from the word width.

CE images: repl 139,642 B (~136.4 KB, ceiling ~137 KB), runner 107,711 B
(~105.2 KB). All four CE benchmarks re-verified at-or-below their recorded
values with exact checksums via deterministic CEmu screen-CRC gates
(control-run-learned PASS-screen CRCs; wrapper pattern in the session
scratchpad). CE size/perf regressions are checked with `tools/map_size.py`
and the CEmu procedure in platform/ti84ce/README.md.
