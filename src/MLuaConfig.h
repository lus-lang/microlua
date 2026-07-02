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
