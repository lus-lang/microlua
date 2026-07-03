/*
 * MicroLua - MLuaMathLib.c
 * Math library implementation
 */

#include "MLuaMathLib.h"

#if MLUA_ENABLE_MATHLIB
#include "../MLuaCore.h"
#include "../MLuaString.h"
#include "../MLuaVM.h"

/* Simple PRNG state for math.random */
static U64 RandomState = 1;

/* ========================================================================== */
/* Helper: Get number argument                                                */
/* ========================================================================== */

static double GetArg(MLuaState *L, int idx) {
  MLuaValue v = MLuaGetStack(L, idx);
  return MLuaToNumber(v);
}

static I64 GetIntArg(MLuaState *L, int idx) { return (I64)GetArg(L, idx); }

/* ========================================================================== */
/* Unary double->double entries                                               */
/* ========================================================================== */

/* One shared body carries the whole arg/convert/push protocol; each entry
 * reduces to an addressable thunk around its (macro) math builtin plus a
 * tail call passing the thunk. This replaces 16 copies of the protocol -
 * a measurable image saving on the size-constrained ports. */
static int Math1(MLuaState *L, double (*fn)(double)) {
  MLuaPush(L, MLuaMakeNumber(L, fn(GetArg(L, 1))));
  return 1;
}

#define MATH1_ENTRY(NAME, EXPR)                                                \
  static double NAME##Thunk(double x) { return (EXPR); }                      \
  static int NAME(MLuaState *L) { return Math1(L, NAME##Thunk); }

MATH1_ENTRY(MathAbs, MathFabs(x))
MATH1_ENTRY(MathAcosF, MathAcos(x))
MATH1_ENTRY(MathAsinF, MathAsin(x))
MATH1_ENTRY(MathCeilF, MathCeil(x))
MATH1_ENTRY(MathCosF, MathCos(x))
MATH1_ENTRY(MathDeg, x * (180.0 / MLUA_PI))
MATH1_ENTRY(MathExpF, MathExp(x))
MATH1_ENTRY(MathFloorF, MathFloor(x))
MATH1_ENTRY(MathRad, x * (MLUA_PI / 180.0))
MATH1_ENTRY(MathSinF, MathSin(x))
MATH1_ENTRY(MathSqrtF, MathSqrt(x))
MATH1_ENTRY(MathTanF, MathTan(x))
MATH1_ENTRY(MathCoshF, MathCosh(x))
MATH1_ENTRY(MathSinhF, MathSinh(x))
MATH1_ENTRY(MathTanhF, MathTanh(x))
MATH1_ENTRY(MathLog10F, MathLog10(x))

/* ========================================================================== */
/* math.atan                                                                  */
/* ========================================================================== */

static int MathAtanF(MLuaState *L) {
  double y = GetArg(L, 1);
  int top = MLuaGetTop(L);
  if (top >= 2) {
    double x = GetArg(L, 2);
    MLuaPush(L, MLuaMakeNumber(L, MathAtan2(y, x)));
  } else {
    MLuaPush(L, MLuaMakeNumber(L, MathAtan(y)));
  }
  return 1;
}

/* ========================================================================== */
/* math.fmod                                                                  */
/* ========================================================================== */

static int MathFmodF(MLuaState *L) {
  double x = GetArg(L, 1);
  double y = GetArg(L, 2);
  MLuaPush(L, MLuaMakeNumber(L, MathFmod(x, y)));
  return 1;
}

/* ========================================================================== */
/* math.pow (Lua 5.1)                                                         */
/* ========================================================================== */

static int MathPowF(MLuaState *L) {
  double x = GetArg(L, 1);
  double y = GetArg(L, 2);
  MLuaPush(L, MLuaMakeNumber(L, MathPow(x, y)));
  return 1;
}

/* ========================================================================== */
/* math.frexp                                                                 */
/* ========================================================================== */

static int MathFrexpF(MLuaState *L) {
  double x = GetArg(L, 1);
  int exp;
  double m = MathFrexp(x, &exp);
  MLuaPush(L, MLuaMakeNumber(L, m));
  MLuaPush(L, MakeInt(exp));
  return 2;
}

/* ========================================================================== */
/* math.ldexp                                                                 */
/* ========================================================================== */

static int MathLdexpF(MLuaState *L) {
  double m = GetArg(L, 1);
  int exp = (int)GetIntArg(L, 2);
  MLuaPush(L, MLuaMakeNumber(L, MathLdexp(m, exp)));
  return 1;
}

/* ========================================================================== */
/* math.log                                                                   */
/* ========================================================================== */

static int MathLogF(MLuaState *L) {
  double x = GetArg(L, 1);
  int top = MLuaGetTop(L);
  if (top >= 2) {
    double base = GetArg(L, 2);
    MLuaPush(L, MLuaMakeNumber(L, MathLog(x) / MathLog(base)));
  } else {
    MLuaPush(L, MLuaMakeNumber(L, MathLog(x)));
  }
  return 1;
}

/* ========================================================================== */
/* math.max                                                                   */
/* ========================================================================== */

static int MathMax(MLuaState *L) {
  int top = MLuaGetTop(L);
  int i;
  double max;
  if (top < 1) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }
  max = GetArg(L, 1);
  for (i = 2; i <= top; i++) {
    double v = GetArg(L, i);
    if (v > max) {
      max = v;
    }
  }
  MLuaPush(L, MLuaMakeNumber(L, max));
  return 1;
}

/* ========================================================================== */
/* math.min                                                                   */
/* ========================================================================== */

static int MathMin(MLuaState *L) {
  int top = MLuaGetTop(L);
  int i;
  double min;
  if (top < 1) {
    MLuaPush(L, MLUA_NIL);
    return 1;
  }
  min = GetArg(L, 1);
  for (i = 2; i <= top; i++) {
    double v = GetArg(L, i);
    if (v < min) {
      min = v;
    }
  }
  MLuaPush(L, MLuaMakeNumber(L, min));
  return 1;
}

/* ========================================================================== */
/* math.modf                                                                  */
/* ========================================================================== */

static int MathModfF(MLuaState *L) {
  /* MLUA_FLOAT locals: the MathModf hook's out-parameter is MLUA_FLOAT*. */
  MLUA_FLOAT x = (MLUA_FLOAT)GetArg(L, 1);
  MLUA_FLOAT ipart;
  MLUA_FLOAT fpart = MathModf(x, &ipart);
  MLuaPush(L, MLuaMakeNumber(L, ipart));
  MLuaPush(L, MLuaMakeNumber(L, fpart));
  return 2;
}

/* ========================================================================== */
/* math.random                                                                */
/* ========================================================================== */

static int MathRandom(MLuaState *L) {
  int top = MLuaGetTop(L);
  double r;

  /* Xorshift64 PRNG */
  RandomState ^= RandomState << 13;
  RandomState ^= RandomState >> 7;
  RandomState ^= RandomState << 17;

  /* Generate random double in [0, 1) */
  r = (double)(RandomState & 0x1FFFFFFFFFFFFFULL) / (double)0x20000000000000ULL;

  if (top == 0) {
    /* No args: return [0, 1) */
    MLuaPush(L, MLuaMakeNumber(L, r));
  } else if (top == 1) {
    /* One arg: return [1, n] */
    I64 n = GetIntArg(L, 1);
    I64 result = 1 + (I64)(r * (double)n);
    if (result > n)
      result = n;
    MLuaPush(L, MLuaMakeInt(L, (I32)result));
  } else {
    /* Two args: return [m, n] */
    I64 m = GetIntArg(L, 1);
    I64 n = GetIntArg(L, 2);
    I64 result = m + (I64)(r * (double)(n - m + 1));
    if (result > n)
      result = n;
    MLuaPush(L, MLuaMakeInt(L, (I32)result));
  }
  return 1;
}

/* ========================================================================== */
/* math.randomseed                                                            */
/* ========================================================================== */

static int MathRandomseed(MLuaState *L) {
  I64 seed = GetIntArg(L, 1);
  RandomState = (U64)seed;
  if (RandomState == 0)
    RandomState = 1; /* Avoid zero state */
  return 0;
}



/* ========================================================================== */
/* math.tointeger                                                             */
/* ========================================================================== */

static int MathTointeger(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  if (IsInt(v)) {
    MLuaPush(L, v);
  } else if (MLuaIsNumber(v)) {
    double d = MLuaToNumber(v);
    double floored = MathFloor(d);
    if (d == floored && floored >= (double)MLUA_INT_MIN &&
        floored <= (double)MLUA_INT_MAX) {
      MLuaPush(L, MLuaMakeInt(L, (I32)floored));
    } else {
      MLuaPush(L, MLUA_NIL);
    }
  } else {
    MLuaPush(L, MLUA_NIL);
  }
  return 1;
}

/* ========================================================================== */
/* math.type                                                                  */
/* ========================================================================== */

static int MathType(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  if (IsInt(v)) {
    MLuaPush(L, MLuaStringNew(L, "integer", 7));
  } else if (MLuaIsNumber(v)) {
    MLuaPush(L, MLuaStringNew(L, "float", 5));
  } else {
    MLuaPush(L, MLUA_NIL);
  }
  return 1;
}

/* ========================================================================== */
/* math.ult                                                                   */
/* ========================================================================== */

static int MathUlt(MLuaState *L) {
  I64 a = GetIntArg(L, 1);
  I64 b = GetIntArg(L, 2);
  /* Unsigned less than comparison */
  MLuaPush(L, MLUA_BOOL((U64)a < (U64)b));
  return 1;
}

/* ========================================================================== */
/* Library Registration                                                       */
/* ========================================================================== */

static const MLuaLibEntry MathLibEntries[] = {{"abs", MathAbs},
                                              {"acos", MathAcosF},
                                              {"asin", MathAsinF},
                                              {"atan", MathAtanF},
                                              {"atan2", MathAtanF},
                                              {"ceil", MathCeilF},
                                              {"cos", MathCosF},
                                              {"cosh", MathCoshF},
                                              {"deg", MathDeg},
                                              {"exp", MathExpF},
                                              {"floor", MathFloorF},
                                              {"fmod", MathFmodF},
                                              {"frexp", MathFrexpF},
                                              {"ldexp", MathLdexpF},
                                              {"log", MathLogF},
                                              {"log10", MathLog10F},
                                              {"max", MathMax},
                                              {"min", MathMin},
                                              {"mod", MathFmodF},
                                              {"pow", MathPowF},
                                              {"modf", MathModfF},
                                              {"rad", MathRad},
                                              {"random", MathRandom},
                                              {"randomseed", MathRandomseed},
                                              {"sin", MathSinF},
                                              {"sinh", MathSinhF},
                                              {"sqrt", MathSqrtF},
                                              {"tan", MathTanF},
                                              {"tanh", MathTanhF},
                                              {"tointeger", MathTointeger},
                                              {"type", MathType},
                                              {"ult", MathUlt},
                                              {NULL, NULL}};

void MLuaOpenMath(MLuaState *L) {
  MLuaValue lib = MLuaNewLib(L, "math");
  MLuaValue key;

  /* Register functions */
  MLuaRegisterLib(L, lib, MathLibEntries);

  /* Register constants */
  key = MLuaStringNew(L, "pi", 2);
  MLuaTableSet(L, lib, key, MLuaMakeNumber(L, MLUA_PI));

  key = MLuaStringNew(L, "huge", 4);
  MLuaTableSet(L, lib, key, MLuaMakeNumber(L, MathHuge));

  key = MLuaStringNew(L, "maxinteger", 10);
  MLuaTableSet(L, lib, key, MLuaMakeInt(L, MLUA_INT_MAX));

  key = MLuaStringNew(L, "mininteger", 10);
  MLuaTableSet(L, lib, key, MLuaMakeInt(L, MLUA_INT_MIN));
}

#endif /* MLUA_ENABLE_MATHLIB */
