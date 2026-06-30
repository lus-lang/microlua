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
  U32 Hash;    /* Precomputed hash value */
  Size Length; /* String length (not including null terminator) */
               /* Followed by char data[Length + 1] */
} MLuaStringHeader;

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

#define MLUA_STRING_TABLE_INITIAL_SIZE 64
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
 * Create a short inline string (up to 3 bytes).
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

#endif /* MLUA_STRING_H */
