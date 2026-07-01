/*
 * MicroLua - MLuaFloatBits.h
 * Integer-only IEEE-754 binary64 <-> binary32 conversion for the width-agnostic
 * bytecode number format. Numbers are always serialized as canonical binary64;
 * on a target whose MLUA_FLOAT is narrower (MLUA_FLOAT_BITS < 64) they are
 * widened on dump and narrowed on undump here, using only U32/U64 bit
 * manipulation (no wider float type, no libc). The default 64-bit build never
 * uses these — DumpNumber/ReadNumber keep their zero-cost reinterpret path.
 */

#ifndef MLUA_FLOAT_BITS_H
#define MLUA_FLOAT_BITS_H

#include "MLuaCore.h"

/* ========================================================================== */
/* Pure bit conversions (always defined; unused in the default build)         */
/* ========================================================================== */

/*
 * Widen an IEEE-754 binary32 bit pattern to binary64. Exact: every binary32
 * value is representable in binary64.
 */
static inline U64 mlua_bits32_to_bits64(U32 x) {
  U32 s = (x >> 31) & 1u;
  U32 e = (x >> 23) & 0xFFu;
  U32 m = x & 0x7FFFFFu;
  U64 sign = (U64)s << 63;

  if (e == 0xFFu) {
    /* Inf (m == 0) or NaN (m != 0); keep it quiet and carry the payload. */
    return m ? (sign | 0x7FF8000000000000ULL | ((U64)m << 29))
             : (sign | 0x7FF0000000000000ULL);
  }
  if (e == 0u) {
    int shift;
    int uexp;
    if (m == 0u) {
      return sign; /* signed zero */
    }
    /* Subnormal binary32: normalize the mantissa into 1.f form. */
    shift = 0;
    while ((m & 0x800000u) == 0u) {
      m <<= 1;
      shift++;
    }
    m &= 0x7FFFFFu;
    uexp = -126 - shift;
    return sign | ((U64)(U32)(uexp + 1023) << 52) | ((U64)m << 29);
  }
  /* Normal. */
  {
    int uexp = (int)e - 127;
    return sign | ((U64)(U32)(uexp + 1023) << 52) | ((U64)m << 29);
  }
}

/*
 * Narrow an IEEE-754 binary64 bit pattern to binary32, round-to-nearest-even.
 * Handles signed zero, subnormals, overflow to Inf, and Inf/NaN.
 */
static inline U32 mlua_bits64_to_bits32(U64 x) {
  U32 s = (U32)((x >> 63) & 1u);
  U32 e = (U32)((x >> 52) & 0x7FFu);
  U64 m = x & 0xFFFFFFFFFFFFFULL; /* 52-bit mantissa */
  U32 sign = s << 31;

  if (e == 0x7FFu) {
    /* Inf (m == 0) or NaN (m != 0). */
    return m ? (sign | 0x7FC00000u) : (sign | 0x7F800000u);
  }
  if (e == 0u) {
    /* binary64 subnormal: magnitude < 2^-1022, far below binary32 min. */
    return sign;
  }
  {
    int uexp = (int)e - 1023;
    if (uexp > 127) {
      return sign | 0x7F800000u; /* overflow -> Inf */
    }
    if (uexp < -126) {
      /* binary32 subnormal or zero. */
      U64 sig, lost, mant, half;
      int shift;
      if (uexp < -150) {
        return sign; /* below half a ULP of the smallest subnormal */
      }
      sig = (1ULL << 52) | m; /* implicit 1 + mantissa, 53 bits */
      shift = -97 - uexp;     /* 30..53 for uexp in [-127..-150] */
      lost = sig & (((U64)1 << shift) - 1);
      mant = sig >> shift;
      half = (U64)1 << (shift - 1);
      if (lost > half || (lost == half && (mant & 1u))) {
        mant++; /* round-to-nearest-even; a carry lands on the smallest normal */
      }
      return sign | (U32)mant;
    }
    /* Normal: drop the low 29 mantissa bits with round-to-nearest-even. */
    {
      U32 exp32 = (U32)(uexp + 127);
      U32 mant = (U32)(m >> 29);
      U32 roundbits = (U32)(m & 0x1FFFFFFFu);
      if (roundbits > 0x10000000u ||
          (roundbits == 0x10000000u && (mant & 1u))) {
        mant++;
        if (mant == 0x800000u) {
          mant = 0u;
          exp32++;
          if (exp32 >= 0xFFu) {
            return sign | 0x7F800000u; /* rounded up into Inf */
          }
        }
      }
      return sign | (exp32 << 23) | (mant & 0x7FFFFFu);
    }
  }
}

/* ========================================================================== */
/* MLUA_FLOAT boundary wrappers (only when the native float is narrower)       */
/* ========================================================================== */

#if MLUA_FLOAT_BITS < 64
MLUA_STATIC_ASSERT(MLUA_FLOAT_BITS == 32,
                   "only binary32 narrowing is implemented");
MLUA_STATIC_ASSERT(sizeof(MLUA_FLOAT) == 4, "MLUA_FLOAT must be binary32 here");

/* Native MLUA_FLOAT (binary32) -> canonical binary64 bits, for dump. */
static inline U64 mlua_f_to_bits64(MLUA_FLOAT f) {
  union {
    MLUA_FLOAT f;
    U32 u;
  } conv;
  conv.f = f;
  return mlua_bits32_to_bits64(conv.u);
}

/* Canonical binary64 bits -> native MLUA_FLOAT (binary32), for undump. */
static inline MLUA_FLOAT mlua_bits64_to_f(U64 bits) {
  union {
    MLUA_FLOAT f;
    U32 u;
  } conv;
  conv.u = mlua_bits64_to_bits32(bits);
  return conv.f;
}
#endif /* MLUA_FLOAT_BITS < 64 */

#endif /* MLUA_FLOAT_BITS_H */
