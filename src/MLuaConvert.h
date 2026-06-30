/*
 * MicroLua - MLuaConvert.h
 * String/Number conversion and formatting utilities
 */

#ifndef MLUA_CONVERT_H
#define MLUA_CONVERT_H

#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Number to String                                                           */
/* ========================================================================== */

/*
 * Convert an integer to a string.
 * @param n      Integer value
 * @param buf    Output buffer (must be at least 32 bytes)
 * @return       Number of characters written (not including null terminator)
 */
Size MLuaIntToStr(I64 n, char *buf);

/*
 * Convert a double to a string.
 * @param d      Double value
 * @param buf    Output buffer (must be at least 64 bytes)
 * @param prec   Decimal precision (-1 for auto)
 * @return       Number of characters written
 */
Size MLuaDoubleToStr(double d, char *buf, int prec);

/*
 * Convert any MLuaValue to a string representation.
 * @param L      State (for allocations)
 * @param v      Value to convert
 * @param buf    Output buffer
 * @param bufLen Buffer size
 * @return       Number of characters written
 */
Size MLuaValueToStr(MLuaState *L, MLuaValue v, char *buf, Size bufLen);

/* ========================================================================== */
/* String to Number                                                           */
/* ========================================================================== */

/*
 * Parse a string as a number.
 * Supports: decimal, hex (0x), scientific notation (e/E)
 * @param s      Input string
 * @param len    String length
 * @param out    Output value
 * @return       TRUE if successful, FALSE if not a valid number
 */
Bool MLuaStrToNumber(const char *s, Size len, double *out);

/*
 * Parse a string as an integer.
 * Supports: decimal, hex (0x), octal (0o), binary (0b)
 * @param s      Input string
 * @param len    String length
 * @param base   Base (0 for auto-detect, 2-36 otherwise)
 * @param out    Output value
 * @return       TRUE if successful
 */
Bool MLuaStrToInt(const char *s, Size len, int base, I64 *out);

/* ========================================================================== */
/* String Formatting                                                          */
/* ========================================================================== */

/*
 * Format a value according to a format specifier.
 * Supports: %s, %d, %i, %u, %o, %x, %X, %f, %e, %E, %g, %G, %c, %q, %%
 * With optional width, precision, and flags.
 *
 * @param L       State
 * @param fmt     Format string
 * @param fmtLen  Format string length
 * @param args    Array of argument values
 * @param nargs   Number of arguments
 * @param buf     Output buffer
 * @param bufLen  Buffer size
 * @return        Number of characters written
 */
Size MLuaFormat(MLuaState *L, const char *fmt, Size fmtLen, MLuaValue *args,
                int nargs, char *buf, Size bufLen);

/*
 * Format a string using C varargs (va_list version).
 * For use internally by MLuaRaise and similar functions.
 * Supports: %s (string), %d (int), %x (unsigned hex), %% (literal %)
 *
 * @param buf     Output buffer
 * @param bufLen  Buffer size
 * @param fmt     Format string (null-terminated)
 * @param args    va_list of arguments
 * @return        Number of characters written (not including null terminator)
 */
#include <stdarg.h>
Size MLuaFormatVA(char *buf, Size bufLen, const char *fmt, va_list args);

#endif /* MLUA_CONVERT_H */
