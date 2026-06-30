/*
 * MicroLua - MLuaValue.h
 * Tagged value representation with alignment-based tagging
 *
 * Per spec: Values are pointer-sized words (upgraded from 32-bit for unlimited
 * heap). Heap objects are 8-byte aligned, leaving low 3 bits for type tags.
 */

#ifndef MLUA_VALUE_H
#define MLUA_VALUE_H

#include "MLuaCore.h"

/* Shared forward declarations (single definition avoids C99 duplicate-typedef) */
typedef struct MLuaState MLuaState;
typedef struct MLuaGCRef MLuaGCRef;

/* ========================================================================== */
/* MLuaValue Type                                                             */
/* ========================================================================== */

/*
 * MLuaValue representation:
 *
 * On 64-bit: NaN-boxing (LuaJIT style)
 *   - Doubles are stored directly with full 64-bit precision
 *   - Other values are encoded as quiet NaN payloads
 *   - A quiet NaN has: exponent=0x7FF, bit 51=1 (quiet), bits 0-50 = payload
 *   - We use pattern: 0xFFF8_0000_0000_0000 | (type << 47) | payload
 *
 * On 32-bit: Alignment-based tagging
 *   - Low 3 bits are type tag (pointers are 8-byte aligned)
 *   - Doubles must be heap-allocated
 */
typedef UPtr MLuaValue;

#if MLUA_PTR_SIZE == 8
/* ========================================================================== */
/* 64-bit NaN-Boxing                                                          */
/* ========================================================================== */

/*
 * NaN-boxing bit layout (64-bit):
 * - Bits 63-52: Exponent (0x7FF for NaN)
 * - Bit 51: Quiet NaN flag (must be 1)
 * - Bits 50-47: Type tag (0-15)
 * - Bits 46-0: Payload (47 bits, enough for pointers)
 *
 * A value is a double if (bits & NANBOX_MASK) != NANBOX_TAG
 */
#define NANBOX_MASK ((U64)0xFFFF000000000000ULL)
#define NANBOX_TAG ((U64)0xFFF8000000000000ULL) /* Quiet NaN base */
#define NANBOX_TYPE_SHIFT 47
#define NANBOX_PAYLOAD_MASK ((U64)0x00007FFFFFFFFFFFULL) /* 47 bits */

/* Type tags within NaN payload */
#define NANTYPE_NIL 0
#define NANTYPE_FALSE 1
#define NANTYPE_TRUE 2
#define NANTYPE_INT 3
#define NANTYPE_PTR 4
#define NANTYPE_LIGHTFUNC 5
#define NANTYPE_SHORTSTR 6
/* 7-15: Reserved */

/* Check if value is a double (not a NaN-boxed value) */
#define IsDouble(v)                                                            \
  (((v) & NANBOX_MASK) != NANBOX_TAG &&                                        \
   ((v) & NANBOX_MASK) != (NANBOX_MASK)) /* Exclude actual NaNs */

/* Actually, simpler: a value is double if high bits are NOT our NaN pattern */
#undef IsDouble
#define IsDouble(v)                                                            \
  ((((v) + ((U64)1 << 51)) & NANBOX_MASK) <=                                   \
   ((U64)0x7FF0000000000000ULL + ((U64)1 << 51)))

/* For simplicity, use: if high 16 bits < 0xFFF8, it's a double */
#undef IsDouble
#define IsDouble(v) (((v) >> 48) < 0xFFF8U)

/* Legacy compatibility macros (map to NaN-boxing) */
#define TAG_PTR 4
#define TAG_INT 3
#define TAG_SPECIAL 0 /* Not used directly */
#define TAG_SHORTSTR 6
#define TAG_LIGHTFUNC 5
#define TAG_FLOAT 0xFF /* Not used in NaN-boxing */
#define TAG_MASK 0     /* Not used in NaN-boxing */
#define TAG_BITS 0     /* Not used in NaN-boxing */

#else /* 32-bit */
/* ========================================================================== */
/* 32-bit Alignment-Based Tagging                                             */
/* ========================================================================== */

#define TAG_PTR 0       /* 000: Heap pointer (mask low 3 bits) */
#define TAG_INT 1       /* 001: Signed integer (upper bits) */
#define TAG_SPECIAL 2   /* 010: nil=0, false=1, true=2 in upper bits */
#define TAG_SHORTSTR 3  /* 011: 3 bytes of inline string data */
#define TAG_LIGHTFUNC 4 /* 100: Index into registered C function table */
#define TAG_FLOAT 5     /* 101: Not used on 32-bit (heap allocated) */
/* 6-7: Reserved */

#define TAG_MASK 7 /* Low 3 bits */
#define TAG_BITS 3

#endif /* MLUA_PTR_SIZE */

/* ========================================================================== */
/* Special Values                                                             */
/* ========================================================================== */

#if MLUA_PTR_SIZE == 8
/* NaN-boxed special values */
#define MLUA_NIL (NANBOX_TAG | ((U64)NANTYPE_NIL << NANBOX_TYPE_SHIFT))
#define MLUA_FALSE (NANBOX_TAG | ((U64)NANTYPE_FALSE << NANBOX_TYPE_SHIFT))
#define MLUA_TRUE (NANBOX_TAG | ((U64)NANTYPE_TRUE << NANBOX_TYPE_SHIFT))
#else
/* 32-bit special values */
#define SPECIAL_NIL 0
#define SPECIAL_FALSE 1
#define SPECIAL_TRUE 2
#define MLUA_NIL ((MLuaValue)((SPECIAL_NIL << TAG_BITS) | TAG_SPECIAL))
#define MLUA_FALSE ((MLuaValue)((SPECIAL_FALSE << TAG_BITS) | TAG_SPECIAL))
#define MLUA_TRUE ((MLuaValue)((SPECIAL_TRUE << TAG_BITS) | TAG_SPECIAL))
#endif

#define MLUA_BOOL(x) ((x) ? MLUA_TRUE : MLUA_FALSE)

/* ========================================================================== */
/* Tag Checking Macros                                                        */
/* ========================================================================== */

#if MLUA_PTR_SIZE == 8
/* NaN-boxing type checks */
#define IsDouble(v) (((v) >> 48) < 0xFFF8U)
#define GetNanType(v) (((v) >> NANBOX_TYPE_SHIFT) & 0xF)
#define IsPtr(v) (!IsDouble(v) && GetNanType(v) == NANTYPE_PTR)
#define IsInt(v) (!IsDouble(v) && GetNanType(v) == NANTYPE_INT)
#define IsShortStr(v) (!IsDouble(v) && GetNanType(v) == NANTYPE_SHORTSTR)
#define IsLightFunc(v) (!IsDouble(v) && GetNanType(v) == NANTYPE_LIGHTFUNC)
#define IsInlineFloat(v) IsDouble(v)
#define IsSpecial(v) (!IsDouble(v) && GetNanType(v) <= NANTYPE_TRUE)
#define GetTag(v) (IsDouble(v) ? 0xFF : GetNanType(v)) /* Compatibility */
#else
/* 32-bit type checks */
#define GetTag(v) ((v) & TAG_MASK)
#define IsPtr(v) (GetTag(v) == TAG_PTR)
#define IsInt(v) (GetTag(v) == TAG_INT)
#define IsSpecial(v) (GetTag(v) == TAG_SPECIAL)
#define IsShortStr(v) (GetTag(v) == TAG_SHORTSTR)
#define IsLightFunc(v) (GetTag(v) == TAG_LIGHTFUNC)
#define IsInlineFloat(v) (0) /* Never inline on 32-bit */
#define IsDouble(v) (0)      /* Use IsNumber instead */
#endif

#define IsNil(v) ((v) == MLUA_NIL)
#define IsFalse(v) ((v) == MLUA_FALSE)
#define IsTrue(v) ((v) == MLUA_TRUE)
#define IsBool(v) (IsFalse(v) || IsTrue(v))

/* Truthiness: everything except nil and false is truthy */
#define IsTruthy(v) (!IsNil(v) && !IsFalse(v))
#define IsFalsy(v) (IsNil(v) || IsFalse(v))

/* ========================================================================== */
/* Value Creation Macros                                                      */
/* ========================================================================== */

#if MLUA_PTR_SIZE == 8
/* NaN-boxing value creation (64-bit) */

/* Pointer: stored in low 47 bits of NaN payload */
#define MakePtr(p)                                                             \
  ((MLuaValue)(NANBOX_TAG | ((U64)NANTYPE_PTR << NANBOX_TYPE_SHIFT) |          \
               ((UPtr)(p) & NANBOX_PAYLOAD_MASK)))
#define GetPtr(v) ((void *)((v) & NANBOX_PAYLOAD_MASK))

/* Integer: stored in low 32 bits of NaN payload */
#define MakeInt(i)                                                             \
  ((MLuaValue)(NANBOX_TAG | ((U64)NANTYPE_INT << NANBOX_TYPE_SHIFT) |          \
               ((U64)(U32)(i))))
#define GetInt(v) ((I32)(U32)((v) & 0xFFFFFFFFULL))

/* Short string: 3 bytes in NaN payload */
#define MakeShortStr(c0, c1, c2)                                               \
  ((MLuaValue)(NANBOX_TAG | ((U64)NANTYPE_SHORTSTR << NANBOX_TYPE_SHIFT) |     \
               ((U64)(U8)(c0) << 16) | ((U64)(U8)(c1) << 8) | (U64)(U8)(c2)))
#define GetShortStrChar0(v) ((char)(((v) >> 16) & 0xFF))
#define GetShortStrChar1(v) ((char)(((v) >> 8) & 0xFF))
#define GetShortStrChar2(v) ((char)((v) & 0xFF))

/* Light function: index in NaN payload */
#define MakeLightFunc(idx)                                                     \
  ((MLuaValue)(NANBOX_TAG | ((U64)NANTYPE_LIGHTFUNC << NANBOX_TYPE_SHIFT) |    \
               (U64)(idx)))
#define GetLightFuncIndex(v) ((Size)((v) & NANBOX_PAYLOAD_MASK))

/* Double: stored directly (no encoding needed) */
static inline MLuaValue MakeDouble(double d) {
  union {
    double d;
    U64 u;
  } conv;
  conv.d = d;
  return (MLuaValue)conv.u;
}
static inline double GetDouble(MLuaValue v) {
  union {
    double d;
    U64 u;
  } conv;
  conv.u = (U64)v;
  return conv.d;
}

#else /* 32-bit */
/* 3-bit tagging value creation (32-bit) */

/* Pointer: must be 8-byte aligned */
#define MakePtr(p) ((MLuaValue)(UPtr)(p))
#define GetPtr(v) ((void *)((v) & ~(UPtr)TAG_MASK))

/* Integer: shift left to make room for tag */
#define MakeInt(i) ((MLuaValue)(((UPtr)(I32)(i) << TAG_BITS) | TAG_INT))
#define GetInt(v) ((I32)((v) >> TAG_BITS))

/* Short string: pack 3 bytes + tag */
#define MakeShortStr(c0, c1, c2)                                               \
  ((MLuaValue)(((UPtr)(U8)(c0) << (TAG_BITS + 16)) |                           \
               ((UPtr)(U8)(c1) << (TAG_BITS + 8)) |                            \
               ((UPtr)(U8)(c2) << TAG_BITS) | TAG_SHORTSTR))
#define GetShortStrChar0(v) ((char)(((v) >> (TAG_BITS + 16)) & 0xFF))
#define GetShortStrChar1(v) ((char)(((v) >> (TAG_BITS + 8)) & 0xFF))
#define GetShortStrChar2(v) ((char)(((v) >> TAG_BITS) & 0xFF))

/* Light function: index into C function table */
#define MakeLightFunc(idx)                                                     \
  ((MLuaValue)(((UPtr)(idx) << TAG_BITS) | TAG_LIGHTFUNC))
#define GetLightFuncIndex(v) ((Size)((v) >> TAG_BITS))

#endif /* MLUA_PTR_SIZE */

/* Integer range limits */
#define MLUA_INT_MAX ((I32)0x7FFFFFFF)
#define MLUA_INT_MIN ((I32)(-0x7FFFFFFF - 1))

/* Backward compatibility alias */
#define GetLightFuncIdx GetLightFuncIndex

/* ========================================================================== */
/* GC Object Header (Per Spec)                                                */
/* ========================================================================== */
/*
 * Every heap object begins with a compact header:
 *   Byte 0: flags = [7:ROM][6:Pinned][5:Marked][4:Reserved][3-0:Type]
 *   Bytes 1+: Variable-length size (Varint/LEB128) for variable-size objects
 *
 * Fixed-size objects (upvalue, proto header) have implicit size.
 */

/* Object types (stored in low 4 bits of flags) */
#define OBJTYPE_STRING 0x01   /* Long string */
#define OBJTYPE_TABLE 0x02    /* Table */
#define OBJTYPE_FUNCTION 0x03 /* Lua closure */
#define OBJTYPE_PROTO 0x04    /* Function prototype/bytecode */
#define OBJTYPE_USERDATA 0x05 /* Userdata */
#define OBJTYPE_UPVALUE 0x06  /* Open upvalue */
#define OBJTYPE_THREAD 0x07   /* Coroutine */
#define OBJTYPE_NUMBER 0x08   /* Heap-allocated floating point */
#define OBJTYPE_RAW 0x09      /* Raw buffer (MLuaAlloc payload): pinned,
                                 always live, no references — makes the GC
                                 heap walk well-defined */

/* Flag bits in header byte */
#define GCFLAG_TYPE_MASK 0x0F /* Low 4 bits: type */
#define GCFLAG_MARKED 0x20    /* Bit 5: marked by GC */
#define GCFLAG_PINNED 0x40    /* Bit 6: pinned (don't move) */
#define GCFLAG_ROM 0x80       /* Bit 7: read-only memory */

/*
 * GC Object Header.
 * CachedSize is the full ALIGNED span the object occupies (header + data +
 * padding) so the linear heap walk steps exactly to the next header.
 * Forward holds the relocation target during a mark-compact cycle; keeping
 * it in the header (Lisp-2's "extra field") means computing addresses never
 * clobbers object data the update phase still needs (Location, Proto, ...).
 */
typedef struct {
  U8 Flags; /* [7:ROM][6:Pinned][5:Marked][4:Reserved][3-0:Type] */
  Size CachedSize; /* Total aligned span including header */
  void *Forward;   /* Compaction forwarding address (GC use only) */
} MLuaGCHeader;

/* Minimum alignment for heap objects */
#define MLUA_ALIGNMENT 8

/* Get object type from header */
#define MLUA_OBJTYPE(h) ((h)->Flags & GCFLAG_TYPE_MASK)

/* Get pointer to object data (after header) */
#define MLUA_OBJDATA(h) ((void *)((U8 *)(h) + sizeof(MLuaGCHeader)))

/* ========================================================================== */
/* Varint Encoding (LEB128-style)                                             */
/* ========================================================================== */

/*
 * Encode a size as varint.
 * Returns number of bytes written.
 */
static inline Size VarintEncode(Size value, U8 *buf) {
  Size count = 0;
  do {
    U8 b = value & 0x7F;
    value >>= 7;
    if (value != 0)
      b |= 0x80; /* More bytes follow */
    buf[count++] = b;
  } while (value != 0);
  return count;
}

/*
 * Decode a varint.
 * Returns number of bytes consumed.
 */
static inline Size VarintDecode(const U8 *buf, Size *value) {
  Size result = 0;
  Size shift = 0;
  Size count = 0;
  U8 b;
  do {
    b = buf[count++];
    result |= (Size)(b & 0x7F) << shift;
    shift += 7;
  } while (b & 0x80);
  *value = result;
  return count;
}

/*
 * Get byte length needed for a varint.
 */
static inline Size VarintLen(Size value) {
  Size len = 1;
  while (value >= 0x80) {
    value >>= 7;
    len++;
  }
  return len;
}

/* ========================================================================== */
/* Type Checking for Heap Objects                                             */
/* ========================================================================== */

/* Check if value is a heap string (not short string) */
static inline Bool IsString(MLuaValue v) {
  if (!IsPtr(v))
    return FALSE;
  MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
  return MLUA_OBJTYPE(h) == OBJTYPE_STRING;
}

/* Check if value is any kind of string */
static inline Bool IsAnyString(MLuaValue v) {
  return IsShortStr(v) || IsString(v);
}

/* Check if value is a table */
static inline Bool IsTable(MLuaValue v) {
  if (!IsPtr(v))
    return FALSE;
  MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
  return MLUA_OBJTYPE(h) == OBJTYPE_TABLE;
}

/* Check if value is a function */
static inline Bool IsFunction(MLuaValue v) {
  if (!IsPtr(v))
    return FALSE;
  MLuaGCHeader *h = (MLuaGCHeader *)GetPtr(v);
  return MLUA_OBJTYPE(h) == OBJTYPE_FUNCTION;
}

/* ========================================================================== */
/* Value Utility Functions                                                    */
/* ========================================================================== */

/* Get human-readable type name */
const char *MLuaTypeName(MLuaValue v);

/* Check if integer fits in tagged format */
Bool MLuaIntFits(I32 i);

/* Create integer, handling overflow */
MLuaValue MLuaMakeIntSafe(I32 i);

/* Get short string length (0-3) */
Size MLuaShortStrLen(MLuaValue v);

/* Create short string from bytes */
MLuaValue MLuaMakeShortStr(const char *s, Size len);

/* Raw equality check */
Bool MLuaRawEqual(MLuaValue a, MLuaValue b);

/* Compare two MLuaValues for integer equality */
Bool MLuaIntEqual(MLuaValue a, MLuaValue b);

/* ========================================================================== */
/* Heap Numbers (for floats that don't fit in tagged integer)                 */
/* ========================================================================== */

/* Heap number structure - stores double value on heap */
typedef struct MLuaNumber {
  MLuaGCHeader Header;
  double Value;
} MLuaNumber;

/* Macro to access number from GC header */
#define MLUA_NUMBER(h) ((MLuaNumber *)(h))

/* Check if value is a number (integer or heap number) */
Bool MLuaIsNumber(MLuaValue v);

/* Get numeric value (from integer or heap number) */
double MLuaToNumber(MLuaValue v);

/* Create heap number if needed */
MLuaValue MLuaMakeNumber(MLuaState *L, double n);

/* Create a floating-point value even when n has an integral value. */
MLuaValue MLuaMakeFloat(MLuaState *L, double n);

#endif /* MLUA_VALUE_H */
