/*
 * MicroLua - MLuaCode.h
 * Bytecode definitions for the pure stack-based VM
 *
 * CRITICAL: All instructions are exactly 1 or 2 bytes.
 * - 1-byte: Stack operations (ADD, SUB, etc.)
 * - 2-byte: Opcode + 8-bit operand (locals, constants, jumps)
 *
 * For indices > 255, use _S (stack) variants that pop the index.
 */

#ifndef MLUA_CODE_H
#define MLUA_CODE_H

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Bytecode Opcodes (Pure Stack-Based VM)                                     */
/* ========================================================================== */
/*
 * Instruction format:
 * - 1-byte: Opcode only
 * - 2-byte: Opcode + B (8-bit operand)
 */

typedef enum {
  /* ===== Constants (0x00-0x06) ===== */
  OP_NOP = 0x00,       /* 1B: —           : No operation */
  OP_LOADNIL = 0x01,   /* 1B: → nil       : Push nil */
  OP_LOADTRUE = 0x02,  /* 1B: → true      : Push true */
  OP_LOADFALSE = 0x03, /* 1B: → false     : Push false */
  OP_LOADINT = 0x04,   /* 2B: B → int     : Push signed 8-bit integer B */
  OP_LOADK = 0x05,     /* 2B: B → val     : Push constants[B] */
  OP_LOADK_S = 0x06,   /* 1B: idx → val   : Pop idx, push constants[idx] */

  /* ===== Locals (0x10-0x13) ===== */
  OP_GETLOCAL = 0x10,       /* 2B: B → val       : Push locals[B] */
  OP_SETLOCAL = 0x11,       /* 2B: B val →       : Pop to locals[B] */
  OP_GETLOCAL_S = 0x12,     /* 1B: idx → val     : Pop idx, push locals[idx] */
  OP_SETLOCAL_S = 0x13,     /* 1B: idx val →     : Pop idx and val, store */
  OP_CLEARLOCAL = 0x0E,     /* 2B: B →           : locals[B] = nil */
  OP_GETLOCAL_CLEAR = 0x0F, /* 2B: B → val       : Push and clear locals[B] */

  /* ===== Arguments (0x14-0x15) ===== */
  OP_GETARG = 0x14, /* 2B: B → val : Push args[B] */
  OP_SETARG = 0x15, /* 2B: B val → : Pop to args[B] */

  /* ===== Upvalues (0x16-0x17) ===== */
  OP_GETUPVAL = 0x16, /* 2B: B → val : Push upvalue[B] */
  OP_SETUPVAL = 0x17, /* 2B: B val → : Pop to upvalue[B] */

  /* ===== Globals (0x18-0x19) - stack based ===== */
  OP_GETGLOBAL = 0x18, /* 1B: key → val     : Pop key, push _G[key] */
  OP_SETGLOBAL = 0x19, /* 1B: key val →     : Pop key and val, _G[key]=val */

  /* ===== Stack Manipulation (0x1A-0x1C) ===== */
  OP_POP = 0x1A,  /* 2B: B vals → : Pop B values */
  OP_DUP = 0x1B,  /* 1B: a → a a  : Duplicate top */
  OP_SWAP = 0x1C, /* 1B: a b → b a : Swap top two */

  /* ===== Upvalue Lifetime (0x1D) ===== */
  OP_CLOSE = 0x1D, /* 2B: B — : Close open upvalues at local slots >= B */

  /* ===== Multi-Result Adjustment (0x1E) ===== */
  OP_ADJUST = 0x1E, /* 2B: B results → vals : Truncate/nil-pad the last
                       call's results (LastCallResults) to exactly B */

  /* ===== Tables (0x20-0x24) ===== */
  OP_NEWTABLE = 0x20, /* 1B: → tbl    : Create and push empty table */
  OP_GETTABLE = 0x21, /* 1B: t k → v  : v = t[k] */
  OP_SETTABLE = 0x22, /* 1B: t k v →  : t[k] = v */
  OP_APPEND = 0x23,   /* 1B: t v →    : t[#t + 1] = v */
  OP_APPENDM = 0x24,  /* 1B: t vals → : Append the last call's results
                         (LastCallResults values) in order */

  /* ===== Logic (0x30-0x34) ===== */
  OP_NOT = 0x30, /* 1B: v → bool   : Push true if v is nil/false */
  OP_EQ = 0x31,  /* 1B: a b → bool : Push a == b */
  OP_LT = 0x32,  /* 1B: a b → bool : Push a < b */
  OP_LE = 0x33,  /* 1B: a b → bool : Push a <= b */
  OP_NEQ = 0x34, /* 1B: a b → bool : Push a ~= b */

  /* ===== Arithmetic (0x40-0x48) ===== */
  OP_ADD = 0x40,  /* 1B: a b → res : a + b */
  OP_SUB = 0x41,  /* 1B: a b → res : a - b */
  OP_MUL = 0x42,  /* 1B: a b → res : a * b */
  OP_DIV = 0x43,  /* 1B: a b → res : a / b (float) */
  OP_IDIV = 0x44, /* 1B: a b → res : a // b (int) */
  OP_MOD = 0x45,  /* 1B: a b → res : a % b */
  OP_POW = 0x46,  /* 1B: a b → res : a ^ b */
  OP_UNM = 0x47,  /* 1B: a → res   : -a */
  OP_LEN = 0x48,  /* 1B: a → int   : #a */

  /* ===== Bitwise (0x50-0x55) ===== */
  OP_BAND = 0x50, /* 1B: a b → res : a & b */
  OP_BOR = 0x51,  /* 1B: a b → res : a | b */
  OP_BXOR = 0x52, /* 1B: a b → res : a ~ b */
  OP_SHL = 0x53,  /* 1B: a b → res : a << b */
  OP_SHR = 0x54,  /* 1B: a b → res : a >> b */
  OP_BNOT = 0x55, /* 1B: a → res   : ~a */

  /* ===== Control Flow (0x60-0x65) ===== */
  OP_JMP = 0x60,    /* 2B: B —         : PC += (I8)B */
  OP_JMPF = 0x61,   /* 2B: B v →       : If falsy: PC += (I8)B */
  OP_JMPT = 0x62,   /* 2B: B v →       : If truthy: PC += (I8)B */
  OP_LOOP = 0x63,   /* 2B: B —         : PC -= B (backward, unsigned) */
  OP_JMP_S = 0x64,  /* 1B: off →       : Pop offset, PC += off (signed) */
  OP_LOOP_S = 0x65, /* 1B: off →       : Pop offset, PC -= off (unsigned) */

  /* ===== Loop Opcodes (0x66-0x6A) - SPEC.FORLOOP.md ===== */
  /* All loop opcodes: 2B format [opcode][base_index], use 4 shadow locals */
  OP_NLOOP_PREP =
      0x66, /* 2B: B body exit step limit start → : Init numeric for */
  OP_NLOOP_STEP =
      0x67, /* 2B: B → idx? : Step numeric loop, push idx if continue */
  OP_GLOOP_SETUP = 0x68, /* 2B: B body func state ctrl → : Init generic for */
  OP_GLOOP_CALL =
      0x69, /* 2B: B → func state ctrl : Push iterator args for CALL */
  OP_GLOOP_STEP =
      0x6A, /* 2B: B results → : Check nil, update ctrl, jump or exit */

  /* ===== Functions (0x70-0x77) ===== */
  OP_CLOSURE = 0x70,   /* 2B: B → func    : Create closure from proto[B] */
  OP_CLOSURE_S = 0x71, /* 1B: idx → func  : Pop idx, create from proto[idx] */
  OP_CALL = 0x72,      /* 2B: B fn args → : Call with B args */
  OP_RET = 0x73,       /* 2B: B vals →    : Return B values */
  OP_RET0 = 0x74,      /* 1B: —           : Return 0 values */
  OP_RET1 = 0x75,      /* 1B: val →       : Return 1 value */
  OP_VARARG = 0x76,    /* 2B: B → vals    : B>0: push B varargs (nil-pad);
                          B==0: push ALL varargs, set LastCallResults */
  OP_TAILCALL = 0x77,  /* 2B: B fn args → : Tail call with B args (reuses
                          the current frame) */
  OP_CALLM = 0x78,     /* 2B: B fn args+ → : Call; arg count is B fixed args
                          plus the last call's results (LastCallResults) */

  /* ===== String (0x80) ===== */
  OP_CONCAT = 0x80, /* 2B: B strs → str : Concatenate top B strings */

  OP_COUNT
} MLuaOpCode;

/* ========================================================================== */
/* Instruction Size Helpers                                                   */
/* ========================================================================== */

/*
 * Get the byte size of an instruction (1, 2, or 3 bytes)
 */
Size MLuaOpSize(MLuaOpCode op);

/* ========================================================================== */
/* Function Prototype                                                         */
/* ========================================================================== */

/* Upvalue description */
typedef struct {
  U8 InStack; /* Is upvalue in parent's stack? */
  U8 Index;   /* Stack slot or outer upvalue index */
} MLuaUpvalDesc;

/* Function prototype - stored in GC heap */
typedef struct MLuaProto MLuaProto;
struct MLuaProto {
  /* Code (variable-length bytecode) */
  U8 *Code;      /* Bytecode bytes */
  Size CodeSize; /* Number of bytes */
  Size CodeCap;  /* Allocated capacity */

  /* Constants */
  MLuaValue *Constants; /* Constant pool (k) */
  Size ConstantsSize;   /* Number of constants */
  Size ConstantsCap;    /* Allocated capacity */

  /* Nested prototypes */
  MLuaProto **Protos; /* Nested function prototypes */
  Size ProtosSize;    /* Number of nested prototypes */

  /* Upvalues */
  MLuaUpvalDesc *Upvalues; /* Upvalue descriptions */
  Size UpvaluesSize;       /* Number of upvalues */

  /* Function info */
  U8 NumParams;    /* Number of fixed parameters */
  U8 NumLocals;    /* Number of local variable slots to reserve */
  U8 IsVararg;     /* Has ... parameter? */
  U8 MaxStackSize; /* Maximum stack depth needed */

  /* Debug info (optional, can be stripped) */
  MLuaValue Source; /* Source file name */
  Size LineDefined; /* Line where function starts */

  /*
   * Line info: Exp-Golomb encoded deltas (legacy, may be removed).
   */
  U8 *LineInfo;      /* Exp-Golomb encoded line deltas */
  Size LineInfoSize; /* Number of bytes in LineInfo */
  Size LineInfoCap;  /* Allocated capacity */

  /*
   * Line map: Array of (PC, Line) pairs for accurate line lookup.
   * Each entry marks "from PC onwards, we're at Line".
   * Sorted by PC for binary search.
   */
  struct {
    Size PC;
    Size Line;
  } *LineMap;
  Size LineMapSize; /* Number of entries */
  Size LineMapCap;  /* Allocated capacity */
};

/* Get prototype pointer from GC object data */
#define MLUA_PROTOHEADER(gch) ((MLuaProto *)MLUA_OBJDATA(gch))

/* ========================================================================== */
/* Code Generator State                                                       */
/* ========================================================================== */

/*
 * A forward jump awaiting its target.
 * Short form: a 2-byte OP_JMP/JMPF/JMPT whose I8 offset is patched in place.
 * Long form (re-parse fallback): the offset lives in a placeholder constant
 * pushed before OP_JMP_S, so any distance is representable.
 */
typedef struct {
  Size Pos; /* Short: position of the jump opcode. Long: position AFTER the
               OP_JMP_S (the offset's origin). */
  int K;    /* -1 for short form; else the placeholder constant index */
} MLuaFwdJump;

/* Compiler function state */
typedef struct MLuaFuncState MLuaFuncState;
struct MLuaFuncState {
  MLuaFuncState *Prev; /* Enclosing function state */
  MLuaState *L;        /* Runtime state */
  MLuaProto *Proto;    /* Function being compiled */

  /* Stack tracking */
  int StackLevel; /* Current stack depth (compile time) */
  int MaxStack;   /* Maximum stack depth seen */

  /* Local variables (compile-time info) */
  struct {
    MLuaValue Name; /* Variable name */
    U8 Slot;        /* FP-relative slot index */
    Size StartPC;   /* Start of scope */
  } *Locals;
  Size LocalsSize; /* Number of locals */
  Size LocalsCap;  /* Capacity */
  U8 NumLocals;    /* Active local count */
  U8 MaxLocals;    /* High-water mark of NumLocals. Loops release their slots
                      at parse time, but the FRAME must reserve the peak:
                      calls inside a loop body allocate the callee frame at
                      LocalsTop, which must clear the live loop slots. */

  /* Upvalues (compile-time descriptors, copied into Proto at body end) */
  struct {
    MLuaValue Name; /* Captured variable name (for dedup) */
    U8 InStack;     /* 1: captures parent local; 0: captures parent upvalue */
    U8 Index;       /* Parent local slot or parent upvalue index */
  } *Upvals;
  Size UpvalsSize; /* Number of upvalues */
  Size UpvalsCap;  /* Capacity */

  /* Highest local slot of THIS function captured by any nested function,
   * or -1 if none. Lets loops skip OP_CLOSE when nothing is captured. */
  int MaxCapturedSlot;
  U32 CapturedSlots[8]; /* 256 bits: slots captured by nested functions */

  /* Lexical scopes whose locals must be cleared when they end. */
  struct {
    U8 NumLocals;
    U8 BreakBoundary;
    Size LocalsSize;
  } *Scopes;
  Size ScopeTop;
  Size ScopeCap;

  /* Code position right after the most recently emitted multi-result
   * instruction (OP_CALL/OP_CALLM/OP_VARARG-all), or 0. When this equals
   * the current position, the expression just parsed ends in a call and
   * may produce any number of values. */
  Size LastCallEnd;

  /* Set when a jump offset exceeded the I8 range (checked at body end) */
  Bool JumpOverflow;

  /* Backpatching for control flow (breaks, if-chain end jumps) */
  MLuaFwdJump *PatchStack; /* Stack of pending forward jumps */
  Size PatchTop;           /* Top of patch stack */
  Size PatchCap;           /* Capacity */

  /* Loop state */
  Size LoopStart; /* Start of current loop (for continue) */

  /* Line number tracking */
  Size LastLine; /* Last source line for which bytecode was emitted */
};

/* ========================================================================== */
/* Code Generator API                                                         */
/* ========================================================================== */

/*
 * Allocate a new function prototype.
 */
MLuaProto *MLuaProtoNew(MLuaState *L);

/*
 * Emit a single opcode (no operands).
 */
Size MLuaEmitOp(MLuaFuncState *fs, MLuaOpCode op);

/*
 * Emit opcode + 8-bit operand.
 */
Size MLuaEmitOpB(MLuaFuncState *fs, MLuaOpCode op, U8 b);

/*
 * Emit raw bytes.
 */
Size MLuaEmitBytes(MLuaFuncState *fs, const U8 *bytes, Size count);

/*
 * Add a constant to the constant pool (deduplicated).
 * Returns the constant index.
 */
int MLuaAddConstant(MLuaFuncState *fs, MLuaValue v);

/*
 * Add a constant WITHOUT deduplication. Use for patchable placeholder
 * constants (e.g. loop targets) whose final value is written after the
 * surrounding code has been emitted. A deduplicated slot could be shared
 * with an unrelated constant, so placeholders must always get a fresh slot.
 */
int MLuaAddConstantRaw(MLuaFuncState *fs, MLuaValue v);

/*
 * Add a string constant.
 */
int MLuaAddStringK(MLuaFuncState *fs, const char *str, Size len);

/*
 * Patch a jump instruction at 'jmp' to target 'target'.
 * The offset is stored as signed 16-bit in the bytes after the opcode.
 */
void MLuaPatchJump(MLuaFuncState *fs, Size jmp, Size target);

/*
 * Get current code position.
 */
Size MLuaCodePos(MLuaFuncState *fs);

/*
 * Get opcode name for debugging.
 */
const char *MLuaOpName(MLuaOpCode op);

/* ========================================================================== */
/* Line Number Info (Exp-Golomb Compression)                                  */
/* ========================================================================== */

/*
 * Emit a line number delta using Exp-Golomb encoding.
 * Delta is signed (line can go backward during compilation).
 */
void MLuaEmitLineDelta(MLuaFuncState *fs, int delta);

/*
 * Track current source line during compilation.
 * Emits line delta if line changed since last call.
 * Call this before emitting bytecode to record line info.
 */
void MLuaEmitLine(MLuaFuncState *fs, Size line);

/*
 * Get the source line number for a given PC offset.
 * Decodes the Exp-Golomb compressed line info.
 */
Size MLuaGetLine(MLuaProto *p, Size pc);

#endif /* MLUA_CODE_H */
