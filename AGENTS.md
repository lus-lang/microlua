# MicroLua — Development Guide

MicroLua (`mlua`) is a tiny, freestanding Lua runtime in C99: NaN-boxed values, a 1–2 byte
stack-machine ISA, single-pass Pratt parser→bytecode, and a Lisp-2 mark-compact GC.
`README.md` is the product spec.

## Build & run

```sh
meson setup builddir                                   # debug: libc allowed, MLUA_DEBUG output
meson setup builddir-release --buildtype=release       # freestanding: -ffreestanding -fno-builtin
ninja -C builddir
./builddir/mlua script.lua                             # run a script
./builddir/mlua                                        # interactive REPL
```

- `-DMLUA_PTR_SIZE=8` is set by meson; required for NaN-boxing on 64-bit.
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
| `src/MLuaValue.c/.h` | Value representation: NaN-boxing (64-bit) / alignment tags (32-bit); GC headers |
| `src/MLuaAlloc.c/.h` | `MLuaState`, bump-pointer heap, constrained/vector state creation, `MLuaGCRef` |
| `src/MLuaGC.c/.h` | Lisp-2 mark-compact GC: mark → compute addresses → update refs → compact |
| `src/MLuaLex.c/.h` | Pull lexer, UTF-8-aware, zero-copy tokens |
| `src/MLuaParse.c` | Single-pass Pratt parser; emits bytecode directly (no AST); jump backpatching |
| `src/MLuaCode.c/.h` | Opcode definitions (1–2 bytes), emission helpers, `MLuaProto`, `MLuaFuncState` |
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
- `MLuaStringData` on short strings (≤3 bytes) returns one of 4 rotating static
  buffers — a pointer is valid until the 4th subsequent call.
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

## Status (2026-06-30, release-candidate hardening)

Debug and freestanding release suites are green locally, including internal C
tests, interpreter suites, smoke tests, CLI bytecode output, security
regressions, and the libc-free guard.
