/*
 * MicroLua - MLuaCore.h
 * Core types and freestanding libc replacements
 */

#ifndef MLUA_CORE_H
#define MLUA_CORE_H

#include "MLuaConfig.h"

/* ========================================================================== */
/* Fixed-width integer types (freestanding)                                   */
/* ========================================================================== */

typedef unsigned char U8;
typedef signed char I8;
typedef unsigned short U16;
typedef signed short I16;
typedef unsigned int U32;
typedef signed int I32;
typedef unsigned long long U64;
typedef signed long long I64;

/* Pointer-sized integer */
#if defined(__LP64__) || defined(_WIN64)
typedef U64 UPtr;
typedef I64 IPtr;
#define UPTR_MAX 0xFFFFFFFFFFFFFFFFULL
#else
typedef U32 UPtr;
typedef I32 IPtr;
#define UPTR_MAX 0xFFFFFFFFU
#endif

/* Size type */
typedef UPtr Size;

/* Boolean */
typedef U8 Bool;
#define TRUE 1
#define FALSE 0

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* ========================================================================== */
/* VM Execution Status                                                        */
/* ========================================================================== */

typedef enum {
  MLUA_OK = 0,      /* Success */
  MLUA_ERRRUN = 1,  /* Runtime error */
  MLUA_ERRMEM = 2,  /* Memory allocation error */
  MLUA_ERRGCMM = 3, /* Error in __gc metamethod */
  MLUA_ERRERR = 4,  /* Error while running error handler */
  MLUA_YIELD = 5    /* Coroutine yielded */
} MLuaStatus;

/* Convenience aliases per SPEC.ERRORS.md */
#define MLUA_ERR_RUNTIME MLUA_ERRRUN
#define MLUA_ERR_SYNTAX MLUA_ERRSYN
#define MLUA_ERR_MEMORY MLUA_ERRMEM

/* ========================================================================== */
/* Utility macros                                                             */
/* ========================================================================== */

#define UNUSED(x) ((void)(x))

/* Alignment */
#define ALIGN_UP(x, align) (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define IS_ALIGNED(x, align) (((x) & ((align) - 1)) == 0)

/* Min/Max */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/* Array length */
#define ARRAY_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))

/* ========================================================================== */
/* Memory functions (freestanding replacements)                               */
/* ========================================================================== */

void *MemCpy(void *dest, const void *src, Size n);
void *MemSet(void *dest, int val, Size n);
void *MemMove(void *dest, const void *src, Size n);
int MemCmp(const void *s1, const void *s2, Size n);

/* ========================================================================== */
/* String functions (freestanding replacements)                               */
/* ========================================================================== */

Size StrLen(const char *s);
int StrCmp(const char *s1, const char *s2);
int StrNCmp(const char *s1, const char *s2, Size n);
char *StrChr(const char *s, int c);

/* ========================================================================== */
/* Debug/Assert (optional, can be disabled)                                   */
/* ========================================================================== */

#ifdef MLUA_DEBUG
extern void MLuaAssertFail(const char *expr, const char *file, int line);
#define MLUA_ASSERT(expr)                                                      \
  ((expr) ? (void)0 : MLuaAssertFail(#expr, __FILE__, __LINE__))
#else
#define MLUA_ASSERT(expr) ((void)0)
#endif

/* ========================================================================== */
/* Math functions (freestanding, using compiler builtins)                     */
/* ========================================================================== */

/*
 * We use __builtin_* intrinsics which are available in GCC and Clang.
 * These compile to native FPU instructions and don't require libc.
 */
#if defined(__GNUC__) || defined(__clang__)
#ifndef MathSin
#define MathSin(x) __builtin_sin(x)
#endif
#ifndef MathCos
#define MathCos(x) __builtin_cos(x)
#endif
#ifndef MathTan
#define MathTan(x) __builtin_tan(x)
#endif
#ifndef MathAsin
#define MathAsin(x) __builtin_asin(x)
#endif
#ifndef MathAcos
#define MathAcos(x) __builtin_acos(x)
#endif
#ifndef MathAtan
#define MathAtan(x) __builtin_atan(x)
#endif
#ifndef MathAtan2
#define MathAtan2(y, x) __builtin_atan2((y), (x))
#endif
#ifndef MathCosh
#define MathCosh(x) __builtin_cosh(x)
#endif
#ifndef MathSinh
#define MathSinh(x) __builtin_sinh(x)
#endif
#ifndef MathTanh
#define MathTanh(x) __builtin_tanh(x)
#endif
#ifndef MathExp
#define MathExp(x) __builtin_exp(x)
#endif
#ifndef MathLog
#define MathLog(x) __builtin_log(x)
#endif
#ifndef MathLog10
#define MathLog10(x) __builtin_log10(x)
#endif
#ifndef MathPow
#define MathPow(x, y) __builtin_pow((x), (y))
#endif
#ifndef MathSqrt
#define MathSqrt(x) __builtin_sqrt(x)
#endif
#ifndef MathFloor
#define MathFloor(x) __builtin_floor(x)
#endif
#ifndef MathCeil
#define MathCeil(x) __builtin_ceil(x)
#endif
#ifndef MathFabs
#define MathFabs(x) __builtin_fabs(x)
#endif
#ifndef MathFmod
#define MathFmod(x, y) __builtin_fmod((x), (y))
#endif
#ifndef MathFrexp
#define MathFrexp(x, exp) __builtin_frexp((x), (exp))
#endif
#ifndef MathLdexp
#define MathLdexp(x, exp) __builtin_ldexp((x), (exp))
#endif
#ifndef MathModf
#define MathModf(x, iptr) __builtin_modf((x), (iptr))
#endif
#ifndef MathIsNan
#define MathIsNan(x) __builtin_isnan(x)
#endif
#ifndef MathIsInf
#define MathIsInf(x) __builtin_isinf(x)
#endif
#ifndef MathHuge
#define MathHuge __builtin_huge_val()
#endif
#else
/* Fallback for other compilers - these would need manual implementation */
#ifndef MathSin
double MathSin(double x);
#endif
#ifndef MathCos
double MathCos(double x);
#endif
#ifndef MathTan
double MathTan(double x);
#endif
#ifndef MathAsin
double MathAsin(double x);
#endif
#ifndef MathAcos
double MathAcos(double x);
#endif
#ifndef MathAtan
double MathAtan(double x);
#endif
#ifndef MathAtan2
double MathAtan2(double y, double x);
#endif
#ifndef MathCosh
double MathCosh(double x);
#endif
#ifndef MathSinh
double MathSinh(double x);
#endif
#ifndef MathTanh
double MathTanh(double x);
#endif
#ifndef MathExp
double MathExp(double x);
#endif
#ifndef MathLog
double MathLog(double x);
#endif
#ifndef MathLog10
double MathLog10(double x);
#endif
#ifndef MathPow
double MathPow(double x, double y);
#endif
#ifndef MathSqrt
double MathSqrt(double x);
#endif
#ifndef MathFloor
double MathFloor(double x);
#endif
#ifndef MathCeil
double MathCeil(double x);
#endif
#ifndef MathFabs
double MathFabs(double x);
#endif
#ifndef MathFmod
double MathFmod(double x, double y);
#endif
#ifndef MathFrexp
double MathFrexp(double x, int *exp);
#endif
#ifndef MathLdexp
double MathLdexp(double x, int exp);
#endif
#ifndef MathModf
double MathModf(double x, double *iptr);
#endif
#ifndef MathIsNan
int MathIsNan(double x);
#endif
#ifndef MathIsInf
int MathIsInf(double x);
#endif
#ifndef MathHuge
#define MathHuge (1.0 / 0.0)
#endif
#endif

/* Pi constant */
#ifndef MLUA_PI
#define MLUA_PI 3.14159265358979323846
#endif

/* Platform integer limits */
#ifndef MLUA_MAXINTEGER
#define MLUA_MAXINTEGER ((I64)0x7FFFFFFFFFFFFFFFLL)
#endif
#ifndef MLUA_MININTEGER
#define MLUA_MININTEGER ((I64)(-0x7FFFFFFFFFFFFFFFLL - 1))
#endif

#endif /* MLUA_CORE_H */
