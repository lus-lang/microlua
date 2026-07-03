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

/* The eZ80 has no alignment requirements, so pack the GC header (3-byte
 * Forward + U32 + U8 = 8 bytes instead of 16): every heap object shrinks by
 * 8 bytes and float/int boxes drop from 24 to 16. Object addresses stay
 * 8-aligned via MLUA_ALIGNMENT; only the payload offset changes. */
#define MLUA_GC_HEADER_ALIGN 1

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

/* Halve the per-function line-map RAM: calculator sources are short, and
 * functions whose bytecode outgrows 64 KB could not fit RAM here anyway. */
#define MLUA_LINE_T U16

/* MLUA_IDX_T deliberately stays at the default (Size) here. U16 would take
 * frames from 24 to 14 bytes (~480 B of arena), but measured on ez80-clang
 * it GROWS the image by ~430 B - 16-bit fields need masking against the
 * eZ80's 24-bit native word - including +52 B inside the RunVM hot loop.
 * Image bytes are scarcer than heap bytes on this device. */

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

/* Word-wise Mem* stays off: UPtr is 32-bit but the eZ80 ALU is 24-bit, so
 * the word loads/stores lower to slower multi-byte sequences while adding
 * ~0.2-0.4 KB of image. The byte loops (or a future LDIR implementation
 * via MLUA_PORT_MEMFUNCS) are the right shape for this machine. */
#define MLUA_MEM_WORDWISE 0

/* MLUA_TABLE_NUM_ARRAYS is NOT set here: the typed-array code measures
 * ~2.8 KB of eZ80 image, which fits the runner target (its makefile opts
 * in) but would leave the full repl build under 1.3 KB of launch headroom.
 * A heap of a few tens of KB cannot afford a 16-byte box per stored float,
 * so the runner - where large precompiled workloads actually run - takes
 * the trade; the repl keeps the image bytes. */

/* The on-calc compiler drops parse-time integer constant folding: it costs
 * ~1 KB of eZ80 image and calculator-typed sources rarely contain foldable
 * constant expressions. Compare-branch fusion stays ON - loops dominate
 * calculator workloads and the fused opcodes win at run time. Bytecode
 * compiled on-calc stays valid v6 either way. (The runner has no compiler,
 * so this knob costs it nothing.) */
#define MLUA_PARSE_FOLD_INT 0

/* Floats are binary32 (~7 digits): nothing on this port can produce an
 * integer needing more than 32 bits of formatting, and 64-bit divide is
 * an eZ80 library call. Emit digits through 32-bit arithmetic. */
#define MLUA_FORMAT_INT64 0

/* Math backend: the MLuaCore.h defaults follow MLUA_FLOAT_BITS, so this
 * port automatically gets the f-suffixed builtins (sinf, powf, ...). */

#endif /* MLUA_PORT_TI84CE_H */
