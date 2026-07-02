/*
 * MicroLua - MLuaString.h
 * Interned string implementation with WTF-8 encoding
 */

#ifndef MLUA_STRING_H
#define MLUA_STRING_H

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* String Object Structure                                                    */
/* ========================================================================== */
/*
 * Strings are stored as:
 * [MLuaGCHeader][MLuaStringHeader][char data...][null terminator]
 *
 * Strings are interned: only one copy of each unique string exists.
 * This allows string equality to be a simple pointer comparison.
 */

typedef struct {
  U32 Hash;   /* Precomputed hash value */
  U32 Length; /* Bit 31: all-ASCII flag; bits 0-30: byte length (not
               * including null terminator). Followed by
               * char data[length + 1]. */
} MLuaStringHeader;

/*
 * Bit 31 of Length caches "every byte < 0x80". Codepoint-indexed operations
 * (string.byte/sub positions) collapse to direct byte offsets on ASCII
 * strings, which otherwise cost a UTF-8 decode of every preceding byte per
 * call. The flag is computed inside loops that already touch every byte
 * (hashing, concat folding), so caching it is free; it is conservative --
 * unset only ever means "take the decoding path". String lengths are
 * capped at 2^31-1 by MLuaStringNew.
 */
#define MLUA_STR_LEN_MASK 0x7FFFFFFFU
#define MLUA_STR_ASCII_BIT 0x80000000U

#define MLuaStrHeaderLen(sh) ((Size)((sh)->Length & MLUA_STR_LEN_MASK))
#define MLuaStrHeaderAscii(sh) (((sh)->Length & MLUA_STR_ASCII_BIT) != 0)

/* Get string data pointer from header */
#define MLUA_STRDATA(strh)                                                     \
  ((const char *)((U8 *)(strh) + sizeof(MLuaStringHeader)))

/* Get string header from GC header */
#define MLUA_STRHEADER(gch) ((MLuaStringHeader *)MLUA_OBJDATA(gch))

/* ========================================================================== */
/* String Hash Table (for interning)                                          */
/* ========================================================================== */
/*
 * The string table is a simple hash table with open addressing.
 * It's stored as part of the runtime state.
 */

/* Initial intern-table capacity (slots hold one MLuaValue each). Also the
 * floor the post-GC shrink pass rehashes down to, so lowering it shrinks the
 * table's resting size too. Must be a power of two. Port-overridable. */
#ifndef MLUA_STRING_TABLE_INITIAL_SIZE
#define MLUA_STRING_TABLE_INITIAL_SIZE 64
#endif
#define MLUA_STRING_TABLE_LOAD_FACTOR 70 /* Resize at 70% load */

/* ========================================================================== */
/* String API                                                                 */
/* ========================================================================== */

/*
 * Initialize the string table in the runtime state.
 */
Bool MLuaStringTableInit(MLuaState *L);

/*
 * Create or retrieve an interned string.
 * If the string already exists, returns the existing value.
 * Otherwise, creates a new string on the heap.
 *
 * @param L    Runtime state
 * @param str  C string to intern
 * @param len  Length of string (or 0 to use strlen)
 * @return     MLuaValue containing the string, or MLUA_NIL on error
 */
MLuaValue MLuaStringNew(MLuaState *L, const char *str, Size len);

/*
 * Create a short inline string (up to MLUA_SHORTSTR_MAX bytes).
 * These are stored directly in the MLuaValue without heap allocation.
 */
MLuaValue MLuaStringNewShort(const char *str, Size len);

/*
 * Get the length of a string value (short or long).
 */
Size MLuaStringLen(MLuaValue v);

/*
 * Get the C string data of a string value.
 * For short strings, data is copied to a static buffer.
 * For long strings, returns pointer to heap data.
 *
 * @param v   String value
 * @return    Pointer to null-terminated C string
 */
const char *MLuaStringData(MLuaValue v);

/*
 * TRUE when every byte of the string is < 0x80 (so codepoint positions
 * equal byte positions). O(1) for long strings (cached flag); short
 * strings check their few inline bytes. Conservative: FALSE only routes
 * callers to the decoding path.
 */
Bool MLuaStringIsAscii(MLuaValue v);

/*
 * Compare two string values for equality.
 * Because strings are interned, this is just a pointer comparison.
 */
Bool MLuaStringEqual(MLuaValue a, MLuaValue b);

/*
 * Compare two string values lexicographically.
 * Returns <0 if a<b, 0 if a==b, >0 if a>b.
 */
int MLuaStringCompare(MLuaValue a, MLuaValue b);

/*
 * Concatenate two strings.
 * Returns a new interned string.
 */
MLuaValue MLuaStringConcat(MLuaState *L, MLuaValue a, MLuaValue b);

/*
 * Concatenate `count` string values (all must be strings) into one interned
 * string, built directly with no temporary buffer or second copy. Used by the
 * VM's concat path for the common all-string case.
 */
MLuaValue MLuaStringConcatMany(MLuaState *L, const MLuaValue *vals, int count);

/*
 * Hash function for strings (FNV-1a)
 */
U32 MLuaStringHash(const char *str, Size len);

/*
 * Shrink and rehash the weak intern table after a GC has tombstoned dead
 * strings. Returns TRUE when a smaller backing table was installed.
 */
Bool MLuaStringTableShrink(MLuaState *L);

#endif /* MLUA_STRING_H */
