# MicroLua

MicroLua (`mlua`) is a tiny, reasonably complete Lua runtime. It is your best friend in embedded systems and other places where portability and footprint are absolutely critical, or when you just want something really small but powerful enough to get the job done.

This is a [Lus](https://lus.dev) subproject for developing the smallest possible Lua runtime that preserves sufficient functionality to remain useful. It aims to be the opposite of other Lua-based projects that traditionally _extends_ the language instead of _reducing_ it even further.

## At a glance

- Provides most of Lua within a tiny footprint.
- No dependence on the C standard library.
- No dynamic memory allocation, pass your own pre-allocated heap to the interpreter[^1].
- Values are NaN-boxed on 64-bit platforms and alignment-tagged on 32-bit platforms.
- Interpreter is implemented as a stack machine, with instructions encoded as single or double bytes.
- Single-pass Pratt parsing and code generation with minimal resource cost across source→bytecode pipeline. Forward jumps use compact 8-bit offsets; chunks that outgrow them are transparently re-compiled with long-form jumps.
- Lisp-2 mark-compact garbage collection that reclaims and defragments the heap to conserve memory. Collection happens at interpreter safepoints, so embedder C code never observes objects moving mid-operation; the string intern table is weak.
- Strings and scripts are encoded as UTF-8, with string operations Unicode-aware: `string.len`, `string.sub`, `string.byte`, `string.char` and `string.reverse` operate on codepoints (not bytes). Case mapping (`upper`/`lower`) is ASCII-only and pattern matching positions are byte-based — full Unicode tables don't fit a tiny footprint.
- Coroutines can yield from any Lua call depth (the dispatch loop is frame-iterative); like Lua 5.1, yielding across a C-call boundary (e.g. from inside a `pcall`ed function) is an error.
- Proper tail calls: `return f(...)` reuses the calling frame, so tail recursion runs in constant frame space.
- Holes in table arrays are runtime errors to ensure heap compactness.
- `math`, `table`, `string`, and `coroutine` libraries are available, with the following supported features:
  - Everything from Lua 5.1 **except** `coroutine.wrap`, `string.gsub`, and `string.gmatch`.
  - **Lua 5.2**: `table.pack` and `table.unpack`.
  - **Lua 5.3**: `string.pack`, `string.packsize`, `string.unpack`, `math.maxinteger`, `math.mininteger`, `math.tointeger`, `math.type`, and `math.ult`. Numeric literals follow the 5.3 integer/float distinction (`math.type(1) == "integer"`, `math.type(1.0) == "float"`).
  - **Lua 5.4**: `coroutine.isyieldable` and `coroutine.close`.
  - The Lua 5.1 base-library conveniences `unpack` (alias of `table.unpack`) and `loadstring` (alias of `load`) are provided; `dofile` and `loadfile` come with the io/os extension[^2].

### Potential blockers

Some compromises had to be made to keep the footprint small. Some features had to be removed, while others had to be re-implemented in a different way. Here are the most important ones:

- _Not_ a drop-in replacement for Lua, unlike [Lus](https://lus.dev).
- All C functions are "light" C functions, meaning they do not get to have upvalues.
- Function environments are not a thing in MicroLua, so `setfenv` and `getfenv` are not available.
- `collectgarbage` is not available due to the tight integration of the garbage collector with the interpreter.
- Metatables are not supported. However, you can still forward table lookups with `table.forward` to replicate the functionality of `__index`. Because of this, `rawequal`, `rawget` and `rawset` are not supported.
- `io` and `os` libraries are only available as optional extensions that require linking against the C standard library (`src/extensions/MLuaStdLib.c`). By default, they are not available and it isn't recommended to use them[^2]. File handles are plain tables carrying their methods (`f:read`, `f:write`, `f:close`, `f:lines`) — no metatables involved.
- `require` is only available if the embedding application provides the necessary callbacks through `MLuaSetRequirer`[^2].
- Default Lua package behavior through `LUA_PATH` and `LUA_CPATH` is not available as `require` behavior is entirely contingent on the embedding application. This also means the `package` global table isn't available.
- Functions emitting to `stdout` or `stderr` (e.g., `print`, `error`) will only have visible output if the embedding application provides the necessary callbacks through `MLuaSetOutput`[^2].

## Building & testing

```sh
meson setup builddir && ninja -C builddir     # debug build (libc allowed)
meson test -C builddir                        # internal C tests + interpreter suites
./builddir/mlua script.lua                    # run a script / start the REPL
```

A freestanding release build (`meson setup builddir-release --buildtype=release`) compiles the core with `-ffreestanding -fno-builtin`. See `CLAUDE.md` for the development guide.

[^1]: You can optionally choose to provide allocators for dynamic memory allocation.
[^2]: These are provided in the repl.
