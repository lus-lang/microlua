This is an early specification of MicroLua. It is more detailed in BROAD and TECHNICAL, but BROAD and TECHNICAL may be missing some key details.

MicroLua is a Lua runtime targeted for embedded systems. It compiles and runs Lua programs using as little as a few kilobytes of ram.

MicroLua only supports a subset of Lua close to 5.1. It implements a stricter mode where some error prone or inefficient Lua constructs are forbidden.

## Changes from Lua 5.1

### User

- Lua 5.3's integer/float distinction is backported.
- Only the following global _functions_ from 5.1 are supported: `assert`, ` error`, `load`, `pairs`, `ipairs`, `next`, `pcall`, `select`, `tonumber`, `tostring`, `type`, `unpack`, and `xpcall`.
  - `require` is enabled **only** if a _requirer_ C function is provided. See below.
  - `print` is enabled **only** if output support is provided. See below.
- Only the following global _libraries_ from 5.1 are supported: `math`, `table`, `coroutine`, and `string`. They are imported by default into every created state.
  - When compiling the default (repl) runtime, 5.1-compatible `io` and `os` global libraries will be available as well. These are only exposed for repl and general programming purposes and are not available in the library version of MicroLua.
- `coroutine.wrap` isn't supported due to light C functions only (see below).
- All C functions are light C functions (lua_pushlightfunction), a concept ported from Lua 5.2; raw C function pointers that have no GC overhead and cannot have upvalues. Only stored as a pointer on the stack.
- Arrays cannot have holes. Writing an element after the end is not allowed:

```lua
a = {}
a[1] = 1	-- Ok to extend the array length.
a.stuff = 2 -- Ok to add fields to tables.
a[3] = 3	-- Errors.
```

- Metatables no longer exist. Therefore, `rawset`, `rawget`, and `rawset` are not implemented. However, you can still set a forwarding table (pseudo \_\_index behavior) with `table.forward`:

```lua
a, b = {}, { v = 1 }

table.forward(a, b) 	-- Missing field indices will forward to b.
assert(a.v == 1)
table.forward(a, nil)	-- Remove the forward.
assert(a.v == nil)
```

- Userdata creation is now done through `microlua_newuserdata`, which now requires a C callback that is invoked when the userdata is freed by the garbage collector, since metatables no longer exist to provide \_\_gc support.

### Internal

- MicroLua's runtime is a strictly stack-based VM instead of a register _and_ stack-based VM. Each instruction is 1 or 2 bytes long. Line number information is compressed with [exponential-Golomb codes](https://en.wikipedia.org/wiki/Exponential-Golomb_coding).
- MicroLua uses a tracing (mark-and-sweep) garbage collector like Lua, but instead of non-moving, it is a moving and compacting garbage collector. It allows smaller objects and avoids memory fragmentation. The GC adds an overhead of a few bits per allocated memory block.
- Each value has the same size as a CPU word (32-bits on a 32-bit CPU, 64-bits on a 64-bit CPU). A value may contain:

  - a 31-bit integer (1 bit tag)
  - a single unicode codepoint (hence a string of one or two 16 bit code units)
  - a 64 bit floating point number with a small exponent with 64 bit CPU words
  - a pointer to a memory block. Memory blocks have a tag stored in memory

- All strings are internally stored in UTF-8. String library functions index all strings as UTF-8 byte sequences.

## C API

### Initialization

MicroLua has almost no dependency on the C library. In particular it does not use `malloc()`, `free()` nor `printf()`. When creating a MicroLua state, you can either provide your own memory buffer with `microlua_newconstrainedstate`, or provide functions for memory management with `microlua_newvectorstate`.

#### Initializing with a memory buffer

```c
lua_State* L;
uint8_t mem_buf[8192];
L = microlua_newconstrainedstate(L, mem_buf, sizeof(mem_buf));
// ...
lua_close(L);
```

#### Initializing with memory management

```c
void* my_own_malloc(lua_State* L, void* ctx, size_t sz);
void my_own_free(lua_State* L, void* ctx, void* ptr);

lua_State* L;
void* ctx = malloc(256); // Can be used to store any allocation information you want.
L = microlua_newvectorstate(L, ctx, my_own_malloc, my_own_free);
// ...
lua_close(L);
free(ctx);
```

### Enabling `require`

To enable usage of `require`, use `microlua_setrequirer` to provide a callback that will be invoked when the runtime invokes `require`. A sample requirer may look like this:

```c
// Pseudo-code. Please do not copy and paste this.
static int require_file(lua_State* L) {
	const char* path = lua_tostring(L, 1);

  if (file_exists(path)) {
    const char* contents = read_file(path);
    if (luaL_loadstring(L, contents) == 0) {
      return 1; // Return the loaded function. This will be cached internally.
    } else {
      luaL_error(L, lua_tostring(L, 3));
    }
  } else {
    luaL_error(L, "'%s' does not exist", path);
  }
}

lua_State* L;
microlua_setrequirer(L, require_file); // 'require' will now be available in globals.
```

### Enabling output support (`print`, error messages, etc.)

To enable outputs, use `microlua_setoutput` to provide a callback that will be invoked when the runtime wants to emit something. The message(s) will be pushed to the stack. A sample printer may look like this:

```c
// Pseudo-code. Please do not copy and paste this.
static void my_print_impl(lua_State* L, int status) {
	formatter f;
  while (lua_gettop(L)) {
    // Iterate over elements, format, do stuff.
    lua_pop(L, 1);
  }

  if (status == MLUA_OUTERROR) {
    // This is an error message created by runtime internals.
    stderr(fmt_string(f));
  } else if (status == MLUA_OUTSYSTEM) {
    // This isn't an error, but it's created by runtime internals.
    stdout("SYSTEM: ", fmt_string(f))
  } else if (status == MLUA_OUTPRINT) {
    // This isn't an error, this is a print() by the user.
    stdout(fmt_string(f));
  }
}

lua_State* L;
microlua_setoutput(L, my_print_impl); // 'print' will now be available in globals.
```
