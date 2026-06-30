/*
 * MicroLua - MLuaUTF8.h
 * UTF-8 / WTF-8 encoding utilities
 *
 * WTF-8 (Wobbly Transformation Format) is a superset of UTF-8 that allows
 * unpaired surrogate code points (U+D800..U+DFFF). This is useful for
 * losslessly encoding Windows filenames which may contain these.
 */

#ifndef MLUA_UTF8_H
#define MLUA_UTF8_H

#include "MLuaCore.h"

/* ========================================================================== */
/* Constants                                                                  */
/* ========================================================================== */

/* Maximum bytes needed to encode a single codepoint */
#define MLUA_UTF8_MAX_BYTES 4

/* Replacement character for invalid sequences */
#define MLUA_UTF8_REPLACEMENT 0xFFFD

/* Surrogate range for WTF-8 */
#define MLUA_SURROGATE_MIN 0xD800
#define MLUA_SURROGATE_MAX 0xDFFF
#define MLUA_HIGH_SURROGATE_MIN 0xD800
#define MLUA_HIGH_SURROGATE_MAX 0xDBFF
#define MLUA_LOW_SURROGATE_MIN 0xDC00
#define MLUA_LOW_SURROGATE_MAX 0xDFFF

/* ========================================================================== */
/* Decoding                                                                   */
/* ========================================================================== */

/*
 * Decode a single UTF-8/WTF-8 codepoint from a byte sequence.
 *
 * @param str     Pointer to UTF-8 bytes
 * @param end     End of string (for bounds checking)
 * @param cpOut   Output: decoded codepoint
 * @return        Number of bytes consumed, or 0 on error
 */
Size MLuaUTF8Decode(const char *str, const char *end, U32 *cpOut);

/*
 * Get the byte length of a UTF-8 sequence starting with the given byte.
 * Returns 0 for invalid lead bytes.
 */
Size MLuaUTF8SeqLen(U8 leadByte);

/*
 * Check if a byte is a UTF-8 continuation byte (10xxxxxx).
 */
Bool MLuaUTF8IsCont(U8 byte);

/*
 * Check if a codepoint is a surrogate (D800..DFFF).
 */
Bool MLuaUTF8IsSurrogate(U32 cp);

/*
 * Validate a UTF-8 string (strict: rejects surrogates).
 * Returns TRUE if valid.
 */
Bool MLuaUTF8Validate(const char *str, Size len);

/*
 * Validate a WTF-8 string (allows surrogates).
 * Returns TRUE if valid.
 */
Bool MLuaWTF8Validate(const char *str, Size len);

/* ========================================================================== */
/* Encoding                                                                   */
/* ========================================================================== */

/*
 * Encode a single codepoint as UTF-8/WTF-8.
 *
 * @param cp   Codepoint to encode (0x0..0x10FFFF, or surrogate for WTF-8)
 * @param buf  Output buffer (must have at least MLUA_UTF8_MAX_BYTES bytes)
 * @return     Number of bytes written, or 0 on error
 */
Size MLuaUTF8Encode(U32 cp, char *buf);

/* ========================================================================== */
/* String Operations                                                          */
/* ========================================================================== */

/*
 * Count the number of codepoints in a UTF-8 string.
 */
Size MLuaUTF8Len(const char *str, Size byteLen);

/*
 * Get the byte offset of the N-th codepoint (0-indexed).
 * Returns byteLen if N >= number of codepoints.
 */
Size MLuaUTF8Offset(const char *str, Size byteLen, Size cpIndex);

/*
 * Advance to the next codepoint.
 * Returns pointer to next codepoint, or end on failure.
 */
const char *MLuaUTF8Next(const char *str, const char *end);

/*
 * Move back to the previous codepoint.
 * Returns pointer to previous codepoint, or start on failure.
 */
const char *MLuaUTF8Prev(const char *str, const char *start);

/* ========================================================================== */
/* Character Classification                                                   */
/* ========================================================================== */

/*
 * Check if a codepoint is a valid identifier start (letter or underscore).
 * Includes Unicode letters for Lua 5.3+ compatibility.
 */
Bool MLuaUTF8IsAlpha(U32 cp);

/*
 * Check if a codepoint is a valid identifier continuation.
 */
Bool MLuaUTF8IsAlnum(U32 cp);

/*
 * Check if a codepoint is whitespace.
 */
Bool MLuaUTF8IsSpace(U32 cp);

/*
 * Check if a codepoint is a decimal digit (0-9).
 */
Bool MLuaUTF8IsDigit(U32 cp);

#endif /* MLUA_UTF8_H */
