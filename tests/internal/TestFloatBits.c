/*
 * MicroLua - TestFloatBits.c
 * Unit tests for the binary64 <-> binary32 bytecode number conversions
 * (src/MLuaFloatBits.h). Compiled with -DMLUA_FLOAT=float -DMLUA_FLOAT_BITS=32
 * so the MLUA_FLOAT boundary wrappers are exercised as well as the pure bit
 * cores. Uses only bit patterns, so it runs on any host.
 */

#include "MLuaCore.h"
#include "MLuaFloatBits.h"
#include <stdio.h>

static int TestsPassed = 0;
static int TestsFailed = 0;

#define TEST(name) static void Test_##name(void)
#define RUN_TEST(name)                                                         \
  do {                                                                         \
    printf("  Testing %s... ", #name);                                         \
    Test_##name();                                                             \
    printf("OK\n");                                                            \
    TestsPassed++;                                                             \
  } while (0)

#define ASSERT(expr)                                                           \
  do {                                                                         \
    if (!(expr)) {                                                             \
      printf("FAILED\n    Assertion failed: %s\n", #expr);                     \
      TestsFailed++;                                                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_U32(got, want)                                                  \
  do {                                                                         \
    U32 g_ = (got), w_ = (want);                                               \
    if (g_ != w_) {                                                            \
      printf("FAILED\n    got 0x%08lX want 0x%08lX\n", (unsigned long)g_,      \
             (unsigned long)w_);                                               \
      TestsFailed++;                                                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define ASSERT_U64(got, want)                                                  \
  do {                                                                         \
    U64 g_ = (got), w_ = (want);                                               \
    if (g_ != w_) {                                                            \
      printf("FAILED\n    got 0x%016llX want 0x%016llX\n",                     \
             (unsigned long long)g_, (unsigned long long)w_);                  \
      TestsFailed++;                                                           \
      return;                                                                  \
    }                                                                          \
  } while (0)

/* ========================================================================== */
/* Widen: binary32 -> binary64 (exact)                                        */
/* ========================================================================== */

TEST(Widen_Known) {
  ASSERT_U64(mlua_bits32_to_bits64(0x00000000u), 0x0000000000000000ULL); /* +0 */
  ASSERT_U64(mlua_bits32_to_bits64(0x80000000u), 0x8000000000000000ULL); /* -0 */
  ASSERT_U64(mlua_bits32_to_bits64(0x3F800000u), 0x3FF0000000000000ULL); /* 1 */
  ASSERT_U64(mlua_bits32_to_bits64(0xBF800000u), 0xBFF0000000000000ULL); /* -1 */
  ASSERT_U64(mlua_bits32_to_bits64(0x40000000u), 0x4000000000000000ULL); /* 2 */
  ASSERT_U64(mlua_bits32_to_bits64(0x3F000000u), 0x3FE0000000000000ULL); /* .5 */
  ASSERT_U64(mlua_bits32_to_bits64(0x7F800000u), 0x7FF0000000000000ULL); /* inf */
  ASSERT_U64(mlua_bits32_to_bits64(0xFF800000u), 0xFFF0000000000000ULL);
}

TEST(Widen_Subnormal) {
  /* smallest binary32 subnormal 2^-149 -> binary64 normal 2^-149 */
  ASSERT_U64(mlua_bits32_to_bits64(0x00000001u), 0x36A0000000000000ULL);
}

TEST(Widen_NaN) {
  U64 q = mlua_bits32_to_bits64(0x7FC00000u);
  ASSERT(((q >> 52) & 0x7FFu) == 0x7FFu);       /* exponent all ones */
  ASSERT((q & 0xFFFFFFFFFFFFFULL) != 0);        /* mantissa nonzero => NaN */
}

/* ========================================================================== */
/* Round-trip: narrow(widen(x)) == x for finite/inf binary32 values           */
/* ========================================================================== */

TEST(RoundTrip_Identity) {
  static const U32 vals[] = {
      0x00000000u, 0x80000000u, 0x3F800000u, 0xBF800000u, 0x40000000u,
      0x3F000000u, 0x4048F5C3u, /* 3.14f */
      0x00000001u, 0x007FFFFFu, /* max subnormal */
      0x00800000u,              /* min normal */
      0x7F7FFFFFu,              /* max normal */
      0x7F800000u, 0xFF800000u, /* +/- inf */
      0xCB3F1234u, 0x12345678u};
  Size i;
  for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
    U32 x = vals[i];
    ASSERT_U32(mlua_bits64_to_bits32(mlua_bits32_to_bits64(x)), x);
  }
}

/* ========================================================================== */
/* Narrow: rounding, overflow, subnormal, specials                            */
/* ========================================================================== */

TEST(Narrow_RoundTiesToEven) {
  /* 1.0 + half a binary32 ULP: ties to even (down) -> 1.0 */
  ASSERT_U32(mlua_bits64_to_bits32(0x3FF0000010000000ULL), 0x3F800000u);
  /* 1.0 + 1.5 binary32 ULP: ties to even (up) -> 1.0 + 2 ULP */
  ASSERT_U32(mlua_bits64_to_bits32(0x3FF0000030000000ULL), 0x3F800002u);
  /* just over half -> rounds up */
  ASSERT_U32(mlua_bits64_to_bits32(0x3FF0000010000001ULL), 0x3F800001u);
}

TEST(Narrow_OverflowToInf) {
  /* 2^128 is above binary32 max -> +Inf */
  ASSERT_U32(mlua_bits64_to_bits32(0x47F0000000000000ULL), 0x7F800000u);
  /* -2^128 -> -Inf */
  ASSERT_U32(mlua_bits64_to_bits32(0xC7F0000000000000ULL), 0xFF800000u);
}

TEST(Narrow_Subnormal) {
  /* 2^-140 -> binary32 subnormal 0x200 (= 2^9 * 2^-149) */
  ASSERT_U32(mlua_bits64_to_bits32(0x3730000000000000ULL), 0x00000200u);
  /* 2^-150 exactly -> ties to even -> 0 */
  ASSERT_U32(mlua_bits64_to_bits32(0x3690000000000000ULL), 0x00000000u);
  /* 1.5 * 2^-150 -> rounds up to the smallest subnormal (0x1) */
  ASSERT_U32(mlua_bits64_to_bits32(0x3698000000000000ULL), 0x00000001u);
  /* 2^-151 (below half a ULP of min subnormal) -> 0 */
  ASSERT_U32(mlua_bits64_to_bits32(0x3680000000000000ULL), 0x00000000u);
}

TEST(Narrow_Specials) {
  ASSERT_U32(mlua_bits64_to_bits32(0x0000000000000000ULL), 0x00000000u); /* +0 */
  ASSERT_U32(mlua_bits64_to_bits32(0x8000000000000000ULL), 0x80000000u); /* -0 */
  ASSERT_U32(mlua_bits64_to_bits32(0x7FF0000000000000ULL), 0x7F800000u); /* inf */
  {
    U32 nan = mlua_bits64_to_bits32(0x7FF8000000000000ULL);
    ASSERT(((nan >> 23) & 0xFFu) == 0xFFu);
    ASSERT((nan & 0x7FFFFFu) != 0);
  }
}

/* ========================================================================== */
/* MLUA_FLOAT wrappers                                                         */
/* ========================================================================== */

TEST(Wrappers_RoundTrip) {
  /* float-representable values survive f -> bits64 -> f exactly */
  MLUA_FLOAT vals[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.5f, 0.75f, -0.5f};
  Size i;
  for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
    MLUA_FLOAT r = mlua_bits64_to_f(mlua_f_to_bits64(vals[i]));
    ASSERT(r == vals[i]);
  }
}

int main(void) {
  printf("Running TestFloatBits (MLUA_FLOAT_BITS=%d)...\n", (int)MLUA_FLOAT_BITS);

  printf("\nWiden:\n");
  RUN_TEST(Widen_Known);
  RUN_TEST(Widen_Subnormal);
  RUN_TEST(Widen_NaN);

  printf("\nRound-trip:\n");
  RUN_TEST(RoundTrip_Identity);

  printf("\nNarrow:\n");
  RUN_TEST(Narrow_RoundTiesToEven);
  RUN_TEST(Narrow_OverflowToInf);
  RUN_TEST(Narrow_Subnormal);
  RUN_TEST(Narrow_Specials);

  printf("\nWrappers:\n");
  RUN_TEST(Wrappers_RoundTrip);

  printf("\n====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);
  return TestsFailed > 0 ? 1 : 0;
}
