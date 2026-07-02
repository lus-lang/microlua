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

/* Computed-goto dispatch (GNU C labels-as-values). Replaces the dispatch
 * switch with a 256-entry label table: one indirect jump per instruction
 * instead of a bounds-checked jump table, typically 10-20% on dispatch-bound
 * code. Costs ~1-2 KB of label table + duplicated dispatch tails, so
 * size-constrained ports should measure before opting in. Off by default;
 * requires GCC or Clang. */
#ifndef MLUA_VM_COMPUTED_GOTO
#define MLUA_VM_COMPUTED_GOTO 0
#endif

#ifndef MLUA_PTR_SIZE
#if defined(__LP64__) || defined(_WIN64)
#define MLUA_PTR_SIZE 8
#else
#define MLUA_PTR_SIZE 4
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

#ifndef MLUA_DEFAULT_STACK_SIZE
#define MLUA_DEFAULT_STACK_SIZE 256
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

/* Static buffer for the stack trace built on runtime errors. The builder
 * clamps every write to the buffer and NUL-terminates, so a smaller buffer
 * just truncates deep traces (one frame line runs ~20-40 bytes). Permanent
 * BSS, so RAM-tight ports may want far less than the default. */
#ifndef MLUA_STACKTRACE_BUF_SIZE
#define MLUA_STACKTRACE_BUF_SIZE 2048
#endif

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
