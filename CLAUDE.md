# MicroLua — Development Guide

MicroLua (`mlua`) is a tiny, freestanding Lua runtime in C99: NaN-boxed values, a 1–2 byte
stack-machine ISA, single-pass Pratt parser→bytecode, and a Lisp-2 mark-compact GC.
`README.md` is the product spec. The documents in `spec/` are early design notes —
inspiration and indicators, **not** hard constraints.

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
  (`src/MLuaRepl.c`) and optional extensions may use libc.
- The interactive REPL evaluates **one line = one chunk**: locals do not persist across
  lines. Multi-line snippets must be run from a file or written on a single line.

## Tests

```sh
meson test -C builddir                                 # internal C tests
cd tests/interpreter && ../../builddir/mlua test_base.lua   # interpreter suites
```

- `require("_base")` resolves relative to the **cwd** — run interpreter tests from
  `tests/interpreter/`.
- Prefer **interpreter tests** (`tests/interpreter/test_*.lua`, harness in `_base.lua`:
  `describe`/`it`/`expect`) over internal C tests; use C tests only for what Lua can't
  reach (GC compaction details, C API edge cases).
- Baseline notes (update as fixed): see "Known baseline" at the bottom.

## Debugging

For crashes, hangs, or timeouts in tests: use **`debug.py`** (an LLDB wrapper that
interrupts after a timeout and prints a backtrace). Do **not** invoke LLDB directly, and
do not "simplify" a failing test to pinpoint a crash — debug the real thing and work
until the crash is fully fixed.

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

- **The GC moves objects.** After *any* allocation, raw pointers into the heap may be
  stale — reload them (e.g. re-`GetPtr` values you still hold) and use `MLuaGCRef` for
  C-held references across allocations.
- Strings are interned: equal contents ⇒ same pointer. Never mutate string bytes in place.
- No metatables anywhere — table delegation goes through the table's `Forward` field
  (`table.forward`). `rawget`/`rawset`/`rawequal`/`setmetatable` must not be added.
- All C functions are **light**: no upvalues, registered via `MLuaRegisterCFunc` /
  `MLuaRegisterLib`. (This is why `coroutine.wrap` is intentionally absent.)
- Table arrays reject holes: writing beyond `len+1` is a runtime error by design.
- `print`/`require` only exist when the embedder installs callbacks
  (`MLuaSetOutput`/`MLuaSetRequirer`) — the REPL installs both.

## Known baseline (2026-06-09)

- `test_vm`: 2 pre-existing failures (`StackPushPop`, `StackIndexing`) — `MLuaGetTop`
  semantics; do not paper over.
- `test_lib`: 1 pre-existing failure (`Load` — `MLuaDoString` status != MLUA_OK).
- Interpreter suites: red until closures/upvalues + `pcall` fixes land (Phase 1) — the
  `_base.lua` harness itself depends on them, plus `io.write` (Phase 4 extension).
