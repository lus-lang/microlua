# MicroLua port configuration

Every knob below is `#ifndef`-guarded, so a port sets it by defining it before
the core sees `MLuaConfig.h`. Select a port with a Meson option — `-Dport=`
`generic64` | `generic32` | `cortex-m` | `riscv32` | `ti84ce` (built-in presets
in this directory), or `-Dport_header=path/to/board.h` — or define
`MLUA_PORT_HEADER` directly. A port header is a plain C header of `#define`s.

## Representation and types

| Knob | Default | What it does |
|---|---|---|
| `MLUA_PORT_HEADER` | *(unset)* | Header `#include`d first by `MLuaConfig.h`; where a board sets its knobs. |
| `MLUA_PTR_SIZE` | `8` if `__LP64__`/`_WIN64` else `4` | Value **representation selector**, not a byte size: `8` → 64-bit NaN-boxing, else → 3-bit alignment tagging (heap floats, boxed integers). |
| `MLUA_ALIGNMENT` | `8` | Heap-object alignment. Must be ≥ 8 on the tagging path (the low 3 bits are the tag). |
| `MLUA_GC_HEADER_ALIGN` | `MLUA_ALIGNMENT` | Alignment (and padded size) of the per-object GC header — i.e. the payload's offset. Object addresses/spans still align to `MLUA_ALIGNMENT`. Ports with no hardware alignment requirements can lower it to pack the header (16 → 8 bytes on a 3-byte-pointer target, shrinking every heap object and cutting number boxes from 24 to 16 bytes); ports that need naturally-aligned payload fields must leave the default. |
| `MLUA_ALIGNAS(n)` | `__attribute__((aligned(n)))` (GCC/Clang) | Alignment attribute for static arenas and the GC header. |
| `MLUA_HAVE_WIDTH_TYPES` | *(unset)* | Define it and typedef `U8`..`I64` yourself if the compiler lacks the `__UINT32_TYPE__` family. |
| `MLUA_USE_STDINT` | *(unset)* | Fallback: derive `U8`..`I32` from the freestanding `<stdint.h>` instead of compiler width macros. |
| `MLUA_FLOAT` | `double` | The float subtype (tagging path's heap number and conversions). Set to `float` for a single-precision runtime. |
| `MLUA_FLOAT_BITS` | `64` | Bit width of `MLUA_FLOAT`. `< 64` enables binary64↔`MLUA_FLOAT` conversion at the bytecode boundary (numbers are always binary64 on disk). Only `32` is implemented. |
| `MLUA_ENABLE_COMPILER` | `1` | `0` drops the lexer/parser; the runtime then loads bytecode only. Wired from Meson `-Dcompiler`. |
| `MLUA_ENABLE_DUMP` | `1` | `0` drops the bytecode serializer (`MLuaDumpFunction`, `string.dump`) for ports with no way to store or transmit dumped chunks. Loading bytecode (`MLuaUndump`) is unaffected. |
| `MLUA_ENABLE_PACK` | `1` | `0` drops `string.pack`/`packsize`/`unpack` (the binary format engine) for ports with no byte-oriented I/O to speak of; worth several KB of image on small targets. |
| `MLUA_VM_COMPUTED_GOTO` | `0` | `1` dispatches through a GNU-C label table (one indirect jump per instruction, no bounds check). Needs GCC/Clang; costs ~1–2 KB of table + dispatch tails, so size-constrained ports should measure before opting in. |
| `MLUA_MEM_WORDWISE` | `1` on GCC/Clang, else `0` | Word-at-a-time bodies in `MemCpy`/`MemMove`/`MemSet` (pointer-width blocks when src/dst are co-aligned). Big on string/GC-heavy hosts; opt out where the native word is narrower than `UPtr` (the eZ80 pays ~0.2–0.4 KB of image for slower code). |
| `MLUA_PORT_MEMFUNCS` | `0` | `1` suppresses the portable `MemCpy`/`MemMove`/`MemSet` definitions so the port links its own (e.g. an eZ80 `LDIR` implementation) — same pattern as the `Math*` hooks. |
| `MLUA_PROFILE_OPS` | `0` | `1` counts every dispatched opcode; `MLuaDumpOpProfile` reports the counts through the output callback. Diagnostic builds only. |

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
| `MLUA_GC_HEADROOM_DIV` | `4` | Headroom-proportional pacing: the per-cycle garbage allowance is at least free-heap/DIV on top of the live-growth percentage. Collapses accumulator-loop collection counts in roomy heaps; degenerates to the classic formula in tight ones. `0` compiles the term out (the TI-84 CE keeps its exact historical pacing). |
| `MLUA_PARSE_MAX_DEPTH` | `256` | Parser recursion bound (C-stack depth, roughly one `ParseExpr` frame per level). Lower it on targets with a small C stack. |
| `MLUA_STACKTRACE_BUF_SIZE` | `2048` | Static BSS buffer the runtime-error stack trace is built into. Writes are clamped and the result is always NUL-terminated, so smaller buffers just truncate deep traces (one frame line runs ~20–40 bytes). |
| `MLUA_STRING_TABLE_INITIAL_SIZE` | `64` | Initial intern-table capacity in slots (one `MLuaValue` each); also the floor the post-GC shrink pass rehashes down to. Power of two. |
| `MLUA_ENABLE_LINEINFO` | `1` | `0` drops the per-function PC→line map: protos lose the map fields (~80 B minimum per function), errors report no line number, traces print `?`. The bytecode format is unchanged (the section is written empty and skipped on load). |
| `MLUA_LINE_T` | `Size` | Type of the in-RAM line-map entry fields (`U16` halves the map on 32-bit targets). Serialization stays fixed-width U32; emit and load saturate at the type's max, keeping the map prefix for oversized functions. |
| `MLUA_IDX_T` | `Size` | Type of call-frame fields (saved PC, arena bases, arg count) and prototype counters. The emitter rejects functions whose bytecode would exceed `MLUA_IDX_MAX` ("function too large"), the loader rejects oversized section counts, and static asserts keep the arena sizes in range. `U16` shrinks every frame and prototype on small ports. |
| `MLUA_TABLE_NUM_ARRAYS` | `0` | `1` (32-bit tagging path only; forced off under NaN-boxing) stores a float-seeded table array part as raw `MLUA_FLOAT` elements instead of value slots pointing at heap boxes — a several-fold cut in retained bytes per element. Reads materialize values through the canonical number constructor (integral values return as plain integers, matching dump/undump canonicalization; non-integral reads allocate, so two reads of one slot are distinct box identities). Unrepresentable stores demote the array to generic slots once, in place. A memory-capacity lever, not a speed one. |

## Numeric limits and math backend (in `MLuaCore.h` / `MLuaValue.h`)

| Knob | Default | What it does |
|---|---|---|
| `MLUA_INT_MAX` / `MLUA_INT_MIN` | I32 range | Integer subtype bounds (integers are 32-bit on every target). |
| `MLUA_INLINE_INT_MAX` / `MLUA_INLINE_INT_MIN` | I32 (NaN-box) / ±2²⁸ (tagging) | Largest value stored inline before spilling to an `OBJTYPE_INT` box. |
| `MLUA_MAXINTEGER` / `MLUA_MININTEGER` | I32 range | `math.maxinteger` / `math.mininteger`. |
| `MLUA_PI` | `3.14159265358979323846` | `math.pi`. |
| `MathSin`, `MathCos`, `MathPow`, … | `__builtin_*`, following `MLUA_FLOAT_BITS` | Math backend. Binary32 runtimes default to the `f`-suffixed builtins (`__builtin_sinf`, …); each hook can still be overridden individually. |

## Worked example — 24-bit `int` / 32-bit `double` / narrow pointer

For a target where `int` is 24-bit, `double` is 32-bit, and pointers fit in 32
bits (e.g. an eZ80-class device — `ti84ce.h` is the real preset for one), the
port header is small — the width-correct types handle the odd `int`, and the
tagging path stores the pointer in a 32-bit value word:

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

Narrow-config knobs get their own full-suite runs (a knob nobody tests rots);
note the multilib cross file's `-m32` must be repeated when overriding `c_args`:

```sh
meson setup build-g32-line16 -Dport=generic32 -Dc_args=-DMLUA_LINE_T=U16
meson setup build-line0      -Dc_args=-DMLUA_ENABLE_LINEINFO=0
meson setup build-m32-hdr    --cross-file cross/x86-multilib.ini \
    -Dc_args="-m32 -DMLUA_GC_HEADER_ALIGN=4" -Dc_link_args=-m32  # packed header
```

The `guard` suite additionally compiles the core under `avr-gcc` (16-bit `int`,
32-bit `double`), a 32-bit `riscv` toolchain, and `ez80-clang` (24-bit `int`
and pointers, with the `ti84ce.h` preset) when present, and links a minimal
freestanding embedder. The full interpreter suite is not expected to
pass under `-DMLUA_FLOAT=float`, since some tests assume binary64 range.
