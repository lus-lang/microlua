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

In a local macOS size build with similar build flags, MicroLua's runtime artifacts total 218 KiB (259 KiB with the parser), around 2.8x smaller than Lua 5.5.0's runtime artifacts at 612 KiB. Re-run `python3 bench/bench.py` to regenerate local comparison results.

## At a glance

- Provides most of Lua within a tiny footprint.
- The core static library has no dependence on the C standard library.
- The core can run without dynamic memory allocation: pass your own pre-allocated heap to the interpreter[^1].
- Interpreter is implemented as a stack machine, with instructions encoded as single or double bytes.
- Single-pass Pratt parsing and code generation with minimal resource cost. The parser/compiler can also be compiled out for bytecode-only embedded targets.
- Garbage collection that reclaims and defragments the heap to conserve memory.
- Holes in table arrays are runtime errors to ensure heap compactness.
- Strings and scripts are encoded as UTF-8.
  - `string.len`, `string.sub`, `string.byte`, `string.char`, and `string.reverse` operate on codepoints.
  - `upper`/`lower` are ASCII-only and pattern matching positions/classes are byte-based.
- `math`, `table`, `string`, and `coroutine` libraries are available, with the following supported features:
  - Everything from Lua 5.1 **except** `coroutine.wrap`, `string.gsub`, and `string.gmatch`.
  - **Lua 5.2**: `table.pack` and `table.unpack`.
  - **Lua 5.3**: `string.pack`, `string.packsize`, `string.unpack`, `math.maxinteger`, `math.mininteger`, `math.tointeger`, `math.type`, and `math.ult`. Numeric literals follow the 5.3 integer/float distinction.
  - **Lua 5.4**: `coroutine.isyieldable` and `coroutine.close`.

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

Port-specific settings are centralized in `src/MLuaConfig.h`. A board port can
override pointer size, heap alignment, default stack/frame sizes, GC threshold,
math hooks, and compiler support by providing one header:

```sh
meson setup build-board -Dport_header=path/to/my_board_mlua.h
```

Built-in presets are available with `-Dport=generic64`, `generic32`,
`cortex-m`, or `riscv32`.

[^1]: You can optionally choose to provide allocators for dynamic memory allocation.

[^2]: These are provided in the repl.
