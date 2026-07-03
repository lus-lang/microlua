# MicroLua

MicroLua (`mlua`) is a tiny, reasonably complete Lua runtime. It is good for embedded systems and other places where portability and footprint are absolutely critical.

```
 * Usage: mlua [options] [file [args]]
 *   -h, --help            List options
 *   -e, --eval EXPR       Evaluate EXPR
 *   -i, --interactive     Go to interactive mode
 *   -I, --include FILE    Include (require) FILE
 *   -d, --dump            Dump memory usage stats
 *       --memory-limit N  Limit memory to N bytes
 *       --no-column       No column in debug info
 *   -o FILE               Save bytecode to FILE
 *   -v, --version         Print version
```

## At a glance

- Provides most of Lua within a tiny footprint.
- The core static library has no dependence on the C standard library.
- The core can run from a caller-provided pre-allocated heap without dynamic memory allocation[^1].
- The interpreter is a compact stack machine whose instructions are always one or two bytes.
- The parser is a single-pass Pratt compiler that emits bytecode directly and can be compiled out for bytecode-only embedded targets.
- Port configuration covers value representation, fixed-width C types, native float width, optional dump/pack engines, computed-goto dispatch, and opcode profiling.
- The garbage collector compacts the heap at safepoints to reclaim memory and remove fragmentation.
- Table arrays reject holes at runtime to keep dense sequences compact.
- Strings and scripts are UTF-8, with codepoint-aware core string operations and deliberately byte/ASCII-oriented pattern matching and case conversion.
- The `math`, `table`, `string`, and `coroutine` libraries include the expected core APIs plus conveniences such as `table.pack`, `table.unpack`, integer math helpers, binary string packing, and coroutine close/yieldability support.

## Memory benchmarks

In a local macOS size build with similar build flags, MicroLua's compiler/parser-less runtime artifacts total 218 KiB (259 KiB with the parser), around 2.8x smaller than Lua 5.5.0's runtime artifacts at 612 KiB. Re-run `python3 bench/bench.py` to regenerate local comparison results.

`python3 bench/benchmarksgame/bench.py` compares MicroLua with Lua 5.5 on
small standalone ports of relevant Lua examples, scaled down for local runs and
adjusted to avoid unsupported MicroLua features such as metatables and generated
`load`. Each row passed a byte-identical stdout correctness gate. Lua memory is
exact peak Lua heap from a tracking `lua_newstate` allocator. MicroLua memory is
exact constrained-heap high-water from a high-limit `--dump` run.

| workload     | source example       | memory-pressure focus    | Lua exact peak | MicroLua exact peak | mlua/lua memory |
| ------------ | -------------------- | ------------------------ | -------------: | ------------------: | --------------: |
| binarytrees  | `binarytrees-lua-4`  | allocation churn         |       1.19 MiB |           546.1 KiB |           0.45x |
| knucleotide  | `knucleotide-lua-2`  | substring table pressure |       76.9 KiB |            71.8 KiB |           0.93x |
| revcomp      | `revcomp-lua-5`      | string builder pressure  |      115.4 KiB |           104.5 KiB |           0.91x |
| spectralnorm | `spectralnorm-lua-1` | numeric arrays           |       25.9 KiB |            25.6 KiB |           0.99x |

### Limitations

Some compromises had to be made to keep the footprint small. Some features had to be removed while others had to be implemented differently:

- All C functions are "light" C functions, meaning they do not get to have upvalues.
- Function environments are not a thing in MicroLua, so `setfenv` and `getfenv` are not available.
- `collectgarbage` is not available due to the tight integration of the garbage collector with the interpreter.
- Metatables are not supported. Because of this, `rawequal`, `rawget` and `rawset` are not available. However, you can still forward table lookups with `table.forward` to replicate the functionality of `__index`.
- `io` and `os` libraries are only available as optional extensions that require linking against the C standard library (`src/extensions/MLuaStdLib.c`). By default, they are not available and it isn't recommended to use them[^2].
- `require` is only available if the embedding application provides the necessary callbacks through `MLuaSetRequirer`[^2].
- Default Lua package behavior through `LUA_PATH` and `LUA_CPATH` is not available as `require` behavior is entirely contingent on the embedding application. This also means the `package` global table isn't available.
- Functions emitting to `stdout` or `stderr` (e.g., `print`, `error`) will only have visible output if the embedding application provides the necessary callbacks through `MLuaSetOutput`[^2].

## Building & testing

```sh
meson setup builddir && ninja -C builddir     # debug build (libc allowed)
meson test -C builddir                        # internal C tests + interpreter suites
meson test -C builddir --suite smoke          # standalone smoke scripts
./builddir/mlua script.lua                    # run a script / start the REPL
```

A freestanding release build (`meson setup builddir-release --buildtype=release`) compiles the core with `-ffreestanding -fno-builtin`.

For embedded deployments that do not need source parsing on the target, build a
bytecode-only runtime:

```sh
meson setup build-bytecode -Dcompiler=false --buildtype=release
ninja -C build-bytecode
./builddir/mlua -o app.mlu app.lua            # host-side precompile step
./build-bytecode/mlua app.mlu                 # target/runtime accepts bytecode
```

When `-Dcompiler=false`, `libmicrolua.a` omits the lexer and parser and exposes
`MLuaLoadBytecode`/`MLuaDoBytecode` for embedded callers.
MicroLua bytecode has a compatibility header, records endianness, serializes
execution-critical fields with fixed widths, and stores numeric constants as
canonical IEEE-754 binary64 values. A target may use a narrower native
`MLUA_FLOAT`, with numeric values narrowed or widened at the bytecode boundary
when the configured format supports it.

Port-specific settings are centralized in `src/MLuaConfig.h`. A board port can
override pointer size, heap alignment, default stack/frame sizes, GC threshold,
fixed-width type source, native float subtype/width, math hooks, and compiler
support by providing one header. Size-sensitive ports can also disable
`string.dump` (`MLUA_ENABLE_DUMP=0`) or `string.pack`/`packsize`/`unpack`
(`MLUA_ENABLE_PACK=0`), enable GNU-C computed-goto dispatch
(`MLUA_VM_COMPUTED_GOTO=1`), or turn on opcode profiling
(`MLUA_PROFILE_OPS=1`) for diagnostics:

```sh
meson setup build-board -Dport_header=path/to/my_board_mlua.h
```

Built-in presets are available with `-Dport=generic64`, `generic32`,
`cortex-m`, `riscv32`, or `ti84ce`. See `src/ports/README.md` for the full
port-knob reference and verification matrix.

## Special ports

Special ports are complete platform integrations that go beyond a Meson port
preset. They may include board-specific build systems, host bindings, packaging
tools, install notes, benchmarks, or emulator test flows.

### TI-84 Plus CE

A complete board port lives in `platform/ti84ce/`: MicroLua on the TI-84
Plus CE (eZ80 — 24-bit `int` and pointers, binary32 `double`), built with
the CE C toolchain as two calculator programs with graphics/keypad/timer
bindings.

| Program | Directory | Contents |
|---|---|---|
| `MLUA.8xp` (~57 KB) | `platform/ti84ce/repl/` | Full build: runs source or bytecode appvars and includes an on-calc REPL |
| `MLUAR.8xp` (~44 KB) | `platform/ti84ce/runner/` | Bytecode-only runner without lexer/parser/compiler |

The CE preset disables `string.dump` and `string.pack` (`MLUA_ENABLE_DUMP=0`,
`MLUA_ENABLE_PACK=0`) and enables computed-goto dispatch because the full REPL
is close to the calculator's practical code-size ceiling. Current CEmu
benchmarks on OS 5.7 show MicroLua beating matched TI-BASIC workloads from
1.01x on list-heavy code to 8.1x on scalar integer loops; see
`platform/ti84ce/README.md` for the full table and autotester workflow.

## License

```
BSD Zero Clause License

Copyright (c) 2026 Louka Ménard Blondin <hello@louka.sh>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH
REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT,
INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
PERFORMANCE OF THIS SOFTWARE.
```

[^1]: You can optionally choose to provide allocators for dynamic memory allocation.

[^2]: These are provided in the repl.
