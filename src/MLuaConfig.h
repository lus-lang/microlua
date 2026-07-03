/*
 * MicroLua - MLuaConfig.h
 * Central port/build configuration.
 */

#ifndef MLUA_CONFIG_H
#define MLUA_CONFIG_H

#ifdef MLUA_PORT_HEADER
#include MLUA_PORT_HEADER
#endif

#ifndef MLUA_ENABLE_COMPILER
#define MLUA_ENABLE_COMPILER 1
#endif

/* Bytecode serialization (MLuaDumpFunction, string.dump). Ports with no way
 * to store or transmit dumped chunks can set this to 0 to drop the
 * serializer; deserialization (MLuaUndump) is unaffected. */
#ifndef MLUA_ENABLE_DUMP
#define MLUA_ENABLE_DUMP 1
#endif

/* Binary (de)serialization helpers string.pack/packsize/unpack. Ports with
 * no byte-oriented I/O to speak of can set this to 0 to drop the format
 * engine from the image. */
#ifndef MLUA_ENABLE_PACK
#define MLUA_ENABLE_PACK 1
#endif

/* Opcode-frequency profiling: the VM counts every dispatched opcode and
 * MLuaDumpOpProfile reports the nonzero counts through the installed output
 * callback. Diagnostic tooling for interpreter performance work; off by
 * default (the counter adds a memory write to every dispatch). */
#ifndef MLUA_PROFILE_OPS
#define MLUA_PROFILE_OPS 0
#endif

/* C-stack buffer ceilings for the assembly paths of print and
 * string.format (and format's argument capture). The defaults suit hosted
 * and multi-KB-stack targets; ports with tiny C stacks (a 6502's 256-byte
 * hardware stack, small 8086 memory models) lower them - output beyond
 * the ceiling truncates, which is the pre-existing behavior at 4096. */
#ifndef MLUA_PRINT_BUF_SIZE
#define MLUA_PRINT_BUF_SIZE 4096
#endif
#ifndef MLUA_FORMAT_BUF_SIZE
#define MLUA_FORMAT_BUF_SIZE 4096
#endif
#ifndef MLUA_FORMAT_MAX_ARGS
#define MLUA_FORMAT_MAX_ARGS 32
#endif

/* Parse-time integer constant folding (60*60*24, -5, ...). Pure emitter
 * feature with exact runtime-identical semantics; the knob exists for
 * ports whose image cannot afford the folding code. */
#ifndef MLUA_PARSE_FOLD_INT
#define MLUA_PARSE_FOLD_INT 1
#endif

/* Parse-time fusion of compare+branch pairs (a < b in if/while/repeat
 * conditions) into the OP_JMPF_* opcodes. The VM handlers are always
 * compiled - any v6 runtime runs any v6 bytecode - so this knob only
 * trades the emitter's code size against fused output on ports whose
 * image cannot afford it. */
#ifndef MLUA_PARSE_FUSE_COMPARE
#define MLUA_PARSE_FUSE_COMPARE 1
#endif

#ifndef MLUA_PTR_SIZE
#if defined(__LP64__) || defined(_WIN64)
#define MLUA_PTR_SIZE 8
#else
#define MLUA_PTR_SIZE 4
#endif
#endif

/* Computed-goto dispatch (GNU C labels-as-values). Replaces the dispatch
 * switch with a 256-entry label table: one indirect jump per instruction
 * instead of a bounds-checked jump table, typically 10-20% on dispatch-bound
 * code, for ~1-2 KB of label table + duplicated dispatch tails. Default ON
 * for GCC/Clang 64-bit hosts (they have the image room); size-constrained
 * 32-bit ports choose per port header (the TI-84 CE opts in, having traded
 * the pack engine for it). */
#ifndef MLUA_VM_COMPUTED_GOTO
#if (defined(__GNUC__) || defined(__clang__)) && MLUA_PTR_SIZE == 8
#define MLUA_VM_COMPUTED_GOTO 1
#else
#define MLUA_VM_COMPUTED_GOTO 0
#endif
#endif

#ifndef MLUA_ALIGNMENT
#define MLUA_ALIGNMENT 8
#endif

/* Alignment (and therefore padded size) of the per-object GC header. Object
 * ADDRESSES and spans always align to MLUA_ALIGNMENT — the tagging path
 * stores its 3 tag bits in the pointer's low bits and needs that. This knob
 * only sets where the payload starts (at sizeof(MLuaGCHeader)). A port whose
 * loads/stores tolerate payload fields at that offset may lower it: on a
 * target with byte alignment and 3-byte pointers the header packs from 16
 * down to 8 bytes, cutting every float/int box from 24 to 16 bytes. Ports
 * that need naturally-aligned payloads must leave the default. */
#ifndef MLUA_GC_HEADER_ALIGN
#define MLUA_GC_HEADER_ALIGN MLUA_ALIGNMENT
#endif

/* Floating-point subtype. Defaults to C `double` (IEEE binary64). A target
 * whose `double` is not 64-bit, or that wants a smaller float, can override
 * MLUA_FLOAT (e.g. `float`) and MLUA_FLOAT_BITS (e.g. 32) in its port header;
 * MLUA_FLOAT_BITS drives the width-agnostic bytecode number conversion. This
 * only affects the 32-bit tagging path's heap number; the 64-bit NaN-boxing
 * path always stores raw binary64. */
#ifndef MLUA_FLOAT
#define MLUA_FLOAT double
#endif
#ifndef MLUA_FLOAT_BITS
#define MLUA_FLOAT_BITS 64
#endif

/* Typed numeric table array parts (32-bit tagging path only; ignored under
 * NaN-boxing, which never heap-allocates numbers). When 1, a table whose
 * FIRST array store is a heap float switches its array part to raw
 * MLUA_FLOAT elements: ~4 bytes retained per element instead of a slot plus
 * a heap box. Reads materialize values through the engine's canonical
 * number constructor (integral values come back as plain integers, exactly
 * as dump/undump already canonicalizes them; non-integral reads allocate a
 * fresh box, so two reads of one slot are distinct box identities - float
 * boxes were already identity-keyed in tables). Storing anything a float
 * cannot hold exactly demotes the array to generic slots, once, in place.
 * A memory-capacity feature for tiny-RAM ports, not a speed feature. */
#ifndef MLUA_TABLE_NUM_ARRAYS
#define MLUA_TABLE_NUM_ARRAYS 0
#endif
#if MLUA_PTR_SIZE == 8
#undef MLUA_TABLE_NUM_ARRAYS
#define MLUA_TABLE_NUM_ARRAYS 0
#endif

#ifndef MLUA_DEFAULT_STACK_SIZE
#define MLUA_DEFAULT_STACK_SIZE 256
#endif

/* Locals arena size, historically welded to the eval-stack size. Separate
 * so RAM-floor ports can shrink one without the other (each slot is one
 * MLuaValue). */
#ifndef MLUA_DEFAULT_LOCALS_SIZE
#define MLUA_DEFAULT_LOCALS_SIZE MLUA_DEFAULT_STACK_SIZE
#endif

/* Smallest heap a state may be created in, and the arena/bookkeeping slack
 * demanded beyond sizeof(MLuaState). The defaults match the historical
 * hard-coded floor; sub-8 KB targets (NES-class WRAM) lower both together
 * with the arena sizes above. */
#ifndef MLUA_MIN_HEAP_SIZE
#define MLUA_MIN_HEAP_SIZE 4096
#endif
#ifndef MLUA_MIN_HEAP_SLACK
#define MLUA_MIN_HEAP_SLACK 2048
#endif

/* GC pacing floors. GROWTH_FLOOR is the smallest allocation budget granted
 * between collections when the heap has room; RESERVE_MIN/MAX clamp the
 * safepoint reserve (see MLuaNextGCThreshold). The defaults assume tens of
 * KB of heap - an 8 KB port lowers GROWTH_FLOOR or every cycle's budget is
 * half its RAM. */
#ifndef MLUA_GC_GROWTH_FLOOR
#define MLUA_GC_GROWTH_FLOOR 4096
#endif
#ifndef MLUA_GC_RESERVE_MIN
#define MLUA_GC_RESERVE_MIN 512
#endif
#ifndef MLUA_GC_RESERVE_MAX
#define MLUA_GC_RESERVE_MAX 8192
#endif

#ifndef MLUA_DEFAULT_ARGS_SIZE
#define MLUA_DEFAULT_ARGS_SIZE 64
#endif

#ifndef MLUA_DEFAULT_FRAMES_SIZE
#define MLUA_DEFAULT_FRAMES_SIZE 64
#endif

#ifndef MLUA_THREAD_EVAL_SIZE
#define MLUA_THREAD_EVAL_SIZE 64
#endif

#ifndef MLUA_THREAD_LOCALS_SIZE
#define MLUA_THREAD_LOCALS_SIZE 64
#endif

#ifndef MLUA_THREAD_ARGS_SIZE
#define MLUA_THREAD_ARGS_SIZE 32
#endif

#ifndef MLUA_THREAD_FRAMES_SIZE
#define MLUA_THREAD_FRAMES_SIZE 16
#endif

#ifndef MLUA_DEFAULT_GC_THRESHOLD_PERCENT
#define MLUA_DEFAULT_GC_THRESHOLD_PERCENT 75
#endif

/* Per-library image knobs: each stdlib can be dropped wholesale from the
 * build (source body and registration both compile away). The base
 * library stays unconditional - assert/pcall/tostring/pairs are the
 * language floor. Dropping the string library also drops the pattern
 * engine and string.format; dropping the coroutine LIBRARY removes the
 * Lua-visible API while the core coroutine machinery (MLuaThread) stays
 * for embedders that drive it from C. */
#ifndef MLUA_ENABLE_MATHLIB
#define MLUA_ENABLE_MATHLIB 1
#endif
#ifndef MLUA_ENABLE_STRINGLIB
#define MLUA_ENABLE_STRINGLIB 1
#endif
#ifndef MLUA_ENABLE_TABLELIB
#define MLUA_ENABLE_TABLELIB 1
#endif
#ifndef MLUA_ENABLE_COROLIB
#define MLUA_ENABLE_COROLIB 1
#endif

/* Integer width of the number-FORMATTING digit emitters (MLuaIntToStr and
 * string.format's %d/%u/%x paths). The default I64 renders any integral
 * binary64 exactly; targets whose runtime int is 32-bit and whose float
 * cannot hold more than ~7 digits anyway (binary32 ports, 8/16-bit cores
 * where 64-bit division is a library call) can set 0 to emit through
 * 32-bit arithmetic. Number PARSING (MLuaStrToInt) keeps I64 either way -
 * literal-range decisions need it. */
#ifndef MLUA_FORMAT_INT64
#define MLUA_FORMAT_INT64 1
#endif

/* Multiply-free hashing. The default string hash is FNV-1a (one 32-bit
 * multiply per byte) and table keys mix with a Knuth multiplicative
 * constant - fine wherever 32-bit multiply is a single instruction, a
 * called routine per byte on the eZ80 and any 8/16-bit core. When 1, both
 * switch to shift-xor mixes (PUC Lua's own string hash shape). Hashes are
 * never serialized (the bytecode loader re-interns from bytes), so this
 * changes distribution only, never compatibility. */
#ifndef MLUA_HASH_SHIFT_XOR
#define MLUA_HASH_SHIFT_XOR 0
#endif

/* Static buffer for the stack trace built on runtime errors. The builder
 * clamps every write to the buffer and NUL-terminates, so a smaller buffer
 * just truncates deep traces (one frame line runs ~20-40 bytes). Permanent
 * BSS, so RAM-tight ports may want far less than the default. */
#ifndef MLUA_STACKTRACE_BUF_SIZE
#define MLUA_STACKTRACE_BUF_SIZE 2048
#endif

/* Index/counter type for call frames and function prototypes (MLuaIdx).
 * Frame fields (saved PC, arena bases, argument count) and proto counters
 * (code/constant/upvalue/proto/line-map sizes) are bounded by the arena
 * sizes and by MLUA_IDX_MAX: the emitter rejects functions whose bytecode
 * would outgrow it ("function too large") and the bytecode loader rejects
 * chunks whose section counts exceed it. Ports whose arenas and functions
 * fit 16 bits can set U16 to shrink every frame and prototype. */
#ifndef MLUA_IDX_T
#define MLUA_IDX_T Size
#endif
#define MLUA_IDX_MAX ((Size)(MLUA_IDX_T)-1)

/* Line-number debug info.
 *
 * MLUA_ENABLE_LINEINFO 0 drops the per-function PC->line map entirely:
 * protos lose the map fields, the parser stops recording lines, and the
 * loader skips the (still present) line-map section of bytecode chunks.
 * Runtime errors then report no line number and stack traces print `?`.
 *
 * MLUA_LINE_T narrows the in-RAM line-map entry fields (each entry is one
 * PC plus one line of this type). The serialized format is fixed-width U32
 * either way; emit and load saturate at MLUA_LINE_MAX, so functions whose
 * code or line numbers outgrow a narrow type keep the map prefix and report
 * the last recorded line beyond it. */
#ifndef MLUA_ENABLE_LINEINFO
#define MLUA_ENABLE_LINEINFO 1
#endif
#ifndef MLUA_LINE_T
#define MLUA_LINE_T Size
#endif
#define MLUA_LINE_MAX ((Size)(MLUA_LINE_T)-1)

#ifndef MLUA_ALIGNAS
#if defined(__GNUC__) || defined(__clang__)
#define MLUA_ALIGNAS(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define MLUA_ALIGNAS(n) __declspec(align(n))
#else
#define MLUA_ALIGNAS(n)
#endif
#endif

#endif /* MLUA_CONFIG_H */
