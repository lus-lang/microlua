# MicroLua port configuration

Every knob below is `#ifndef`-guarded, so a port sets it by defining it before
the core sees `MLuaConfig.h`. Select a port with a Meson option — `-Dport=`
`generic64` | `generic32` | `cortex-m` | `riscv32` (built-in presets in this
directory), or `-Dport_header=path/to/board.h` — or define `MLUA_PORT_HEADER`
directly. A port header is a plain C header of `#define`s.

## Representation and types

| Knob | Default | What it does |
|---|---|---|
| `MLUA_PORT_HEADER` | *(unset)* | Header `#include`d first by `MLuaConfig.h`; where a board sets its knobs. |
| `MLUA_PTR_SIZE` | `8` if `__LP64__`/`_WIN64` else `4` | Value **representation selector**, not a byte size: `8` → 64-bit NaN-boxing, else → 3-bit alignment tagging (heap floats, boxed integers). |
| `MLUA_ALIGNMENT` | `8` | Heap-object alignment. Must be ≥ 8 on the tagging path (the low 3 bits are the tag). |
| `MLUA_ALIGNAS(n)` | `__attribute__((aligned(n)))` (GCC/Clang) | Alignment attribute for static arenas and the GC header. |
| `MLUA_HAVE_WIDTH_TYPES` | *(unset)* | Define it and typedef `U8`..`I64` yourself if the compiler lacks the `__UINT32_TYPE__` family. |
| `MLUA_USE_STDINT` | *(unset)* | Fallback: derive `U8`..`I32` from the freestanding `<stdint.h>` instead of compiler width macros. |
| `MLUA_FLOAT` | `double` | The float subtype (tagging path's heap number and conversions). Set to `float` for a single-precision runtime. |
| `MLUA_FLOAT_BITS` | `64` | Bit width of `MLUA_FLOAT`. `< 64` enables binary64↔`MLUA_FLOAT` conversion at the bytecode boundary (numbers are always binary64 on disk). Only `32` is implemented. |
| `MLUA_ENABLE_COMPILER` | `1` | `0` drops the lexer/parser; the runtime then loads bytecode only. Wired from Meson `-Dcompiler`. |
| `MLUA_ENABLE_DUMP` | `1` | `0` drops the bytecode serializer (`MLuaDumpFunction`, `string.dump`) for ports with no way to store or transmit dumped chunks. Loading bytecode (`MLuaUndump`) is unaffected. |

## Arenas and GC (default constrained state)

| Knob | Default | What it does |
|---|---|---|
| `MLUA_DEFAULT_STACK_SIZE` | `256` | Operand-stack slots. |
| `MLUA_DEFAULT_ARGS_SIZE` | `64` | Call-argument window slots. |
| `MLUA_DEFAULT_FRAMES_SIZE` | `64` | Call-frame depth. |
| `MLUA_THREAD_EVAL_SIZE` | `64` | Per-coroutine operand-stack slots. |
| `MLUA_THREAD_LOCALS_SIZE` | `64` | Per-coroutine locals slots. |
| `MLUA_THREAD_ARGS_SIZE` | `32` | Per-coroutine argument slots. |
| `MLUA_THREAD_FRAMES_SIZE` | `16` | Per-coroutine frame depth. |
| `MLUA_DEFAULT_GC_THRESHOLD_PERCENT` | `75` | Heap-fill percent that triggers a collection. |
| `MLUA_PARSE_MAX_DEPTH` | `256` | Parser recursion bound (C-stack depth, roughly one `ParseExpr` frame per level). Lower it on targets with a small C stack. |

## Numeric limits and math backend (in `MLuaCore.h` / `MLuaValue.h`)

| Knob | Default | What it does |
|---|---|---|
| `MLUA_INT_MAX` / `MLUA_INT_MIN` | I32 range | Integer subtype bounds (integers are 32-bit on every target). |
| `MLUA_INLINE_INT_MAX` / `MLUA_INLINE_INT_MIN` | I32 (NaN-box) / ±2²⁸ (tagging) | Largest value stored inline before spilling to an `OBJTYPE_INT` box. |
| `MLUA_MAXINTEGER` / `MLUA_MININTEGER` | I32 range | `math.maxinteger` / `math.mininteger`. |
| `MLUA_PI` | `3.14159265358979323846` | `math.pi`. |
| `MathSin`, `MathCos`, `MathPow`, … | `__builtin_*`, following `MLUA_FLOAT_BITS` | Math backend. Binary32 runtimes default to the `f`-suffixed builtins (`__builtin_sinf`, …); each hook can still be overridden individually. |

## Worked example — 24-bit `int` / 32-bit `double` / 32-bit pointer

For a target where `int` is 24-bit, `double` is 32-bit, and pointers are 32-bit
(e.g. an eZ80-class device), the port header is small — the width-correct types
handle the odd `int`, and the tagging path handles the 32-bit pointer:

```c
#ifndef MLUA_PORT_EZ80_H
#define MLUA_PORT_EZ80_H

#define MLUA_PTR_SIZE 4      /* tagging representation (32-bit value word) */
#define MLUA_FLOAT float     /* native double is only 32-bit */
#define MLUA_FLOAT_BITS 32   /* narrow the canonical binary64 bytecode on load */

/* Shrink the arenas to fit tight RAM. */
#define MLUA_DEFAULT_STACK_SIZE 128
#define MLUA_DEFAULT_ARGS_SIZE 32
#define MLUA_DEFAULT_FRAMES_SIZE 32

/* Math backend: nothing to do - MLUA_FLOAT_BITS == 32 makes the defaults
 * resolve to the f-suffixed builtins (__builtin_sinf, ...). */

#endif
```

With the width types derived from the compiler (`U32` = `__UINT32_TYPE__`),
nothing else in the core needs touching. Compile-time asserts reject a header
that is internally inconsistent (e.g. NaN-boxing requested on a 32-bit word).

## Verifying a port

Configure a build-dir per target and run `meson test`; the validation matrix is:

```sh
meson setup builddir                                        # host, NaN-boxing
meson setup build-g32   -Dport=generic32                    # tagging representation
meson setup build-m32   --cross-file cross/x86-multilib.ini # true 32-bit word
meson setup build-f32   -Dport=generic32 \
    -Dc_args="-DMLUA_FLOAT=float -DMLUA_FLOAT_BITS=32"       # single precision
meson test -C <dir>                                         # (float: --suite bytecode)
```

The `guard` suite additionally compiles the core under `avr-gcc` (16-bit `int`,
32-bit `double`) and a 32-bit `riscv` toolchain when present, and links a
minimal freestanding embedder. The full interpreter suite is not expected to
pass under `-DMLUA_FLOAT=float`, since some tests assume binary64 range.
