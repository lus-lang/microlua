/*
 * TI-84 Plus CE (eZ80) MicroLua port preset.
 *
 * eZ80 as seen by ez80-clang: 24-bit int and pointers, 32-bit long,
 * double == float == IEEE binary32 (soft float), no alignment requirements.
 * The width-correct core types absorb the 24-bit int; the tagging path
 * stores 24-bit pointers in a 32-bit value word losslessly.
 */

#ifndef MLUA_PORT_TI84CE_H
#define MLUA_PORT_TI84CE_H

#define MLUA_PTR_SIZE 4  /* tagging representation (32-bit value word) */
#define MLUA_ALIGNMENT 8 /* allocator-enforced; frees low 3 bits for tags */

/* Native double is binary32; narrow canonical binary64 bytecode on load. */
#define MLUA_FLOAT float
#define MLUA_FLOAT_BITS 32

/* Arenas sized for a Lua heap of a few tens of KB inside ~60 KB of RAM. */
#define MLUA_DEFAULT_STACK_SIZE 128
#define MLUA_DEFAULT_ARGS_SIZE 32
#define MLUA_DEFAULT_FRAMES_SIZE 48
#define MLUA_THREAD_EVAL_SIZE 32
#define MLUA_THREAD_LOCALS_SIZE 32
#define MLUA_THREAD_ARGS_SIZE 16
#define MLUA_THREAD_FRAMES_SIZE 12
#define MLUA_DEFAULT_GC_THRESHOLD_PERCENT 60

/* The OS grants ~4 KB of C stack; bound parser recursion well inside it. */
#define MLUA_PARSE_MAX_DEPTH 32

/* ~12 trace lines fit the 10-line home screen; the frame cap is 48, so deep
 * traces truncate rather than reserving 2 KB of scarce BSS. */
#define MLUA_STACKTRACE_BUF_SIZE 512

/* No way to store or send a dumped chunk from the calculator, and the full
 * build only barely fits user RAM - drop the bytecode serializer. */
#define MLUA_ENABLE_DUMP 0

/* Same footprint reasoning for the string.pack format engine: the
 * calculator has no byte-oriented I/O to pack for. */
#define MLUA_ENABLE_PACK 0

/* Dropping the pack engine bought the image room for computed-goto
 * dispatch, which is worth more per byte here: the eZ80 pays heavily for
 * the switch's bounds check + jump on every instruction. */
#define MLUA_VM_COMPUTED_GOTO 1

/* Math backend: the MLuaCore.h defaults follow MLUA_FLOAT_BITS, so this
 * port automatically gets the f-suffixed builtins (sinf, powf, ...). */

#endif /* MLUA_PORT_TI84CE_H */
