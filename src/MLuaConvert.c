/*
 * MicroLua - MLuaConvert.c
 * String/Number conversion and formatting utilities
 */

#include "MLuaConvert.h"
#include "MLuaString.h"

/* ========================================================================== */
/* Integer to String                                                          */
/* ========================================================================== */

Size MLuaIntToStr(I64 n, char *buf) {
  char tmp[32];
  int i = 0;
  Size len = 0;
  Bool neg = FALSE;

  if (n < 0) {
    neg = TRUE;
    n = -n;
  }

  /* Handle zero */
  if (n == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return 1;
  }

  /* Build digits in reverse */
  while (n > 0) {
    tmp[i++] = '0' + (int)(n % 10);
    n /= 10;
  }

  /* Add sign */
  if (neg) {
    buf[len++] = '-';
  }

  /* Reverse digits */
  while (i > 0) {
    buf[len++] = tmp[--i];
  }

  buf[len] = '\0';
  return len;
}

/* ========================================================================== */
/* Double to String                                                           */
/* ========================================================================== */

Size MLuaDoubleToStr(double d, char *buf, int prec) {
  Size len = 0;
  Bool neg = FALSE;
  I64 intPart;
  double fracPart;
  int i;
  char tmp[32];
  int ti = 0;

  /* Handle special cases */
  if (d != d) { /* NaN */
    buf[0] = 'n';
    buf[1] = 'a';
    buf[2] = 'n';
    buf[3] = '\0';
    return 3;
  }

  if (d == MathHuge) {
    buf[0] = 'i';
    buf[1] = 'n';
    buf[2] = 'f';
    buf[3] = '\0';
    return 3;
  }

  if (d == -MathHuge) {
    buf[0] = '-';
    buf[1] = 'i';
    buf[2] = 'n';
    buf[3] = 'f';
    buf[4] = '\0';
    return 4;
  }

  /* Handle negative */
  if (d < 0) {
    neg = TRUE;
    d = -d;
  }

  /* Default precision */
  if (prec < 0)
    prec = 14;
  if (prec > 32)
    prec = 32;

  /* Split into integer and fractional parts */
  intPart = (I64)d;
  fracPart = d - (double)intPart;

  /* Integer part */
  if (intPart == 0) {
    tmp[ti++] = '0';
  } else {
    I64 t = intPart;
    while (t > 0) {
      tmp[ti++] = '0' + (int)(t % 10);
      t /= 10;
    }
  }

  if (neg)
    buf[len++] = '-';
  while (ti > 0)
    buf[len++] = tmp[--ti];

  /* Fractional part */
  if (prec > 0 && fracPart > 0) {
    buf[len++] = '.';

    for (i = 0; i < prec; i++) {
      fracPart *= 10.0;
      int digit = (int)fracPart;
      buf[len++] = '0' + digit;
      fracPart -= digit;

      /* Stop if remaining fraction is negligible */
      if (fracPart < 1e-15)
        break;
    }

    /* Remove trailing zeros */
    while (len > 1 && buf[len - 1] == '0')
      len--;
    if (buf[len - 1] == '.')
      len--;
  }

  buf[len] = '\0';
  return len;
}

/* ========================================================================== */
/* Value to String                                                            */
/* ========================================================================== */

/*
 * Helper: Write hex pointer address to buffer (without 0x prefix)
 */
static Size WriteHexPtr(void *ptr, char *buf, Size bufLen) {
  UPtr p = (UPtr)ptr;
  char tmp[32];
  int ti = 0;
  Size len = 0;
  static const char hex[] = "0123456789abcdef";

  if (p == 0) {
    if (bufLen >= 2) {
      buf[0] = '0';
      buf[1] = '\0';
      return 1;
    }
    return 0;
  }

  /* Build hex digits in reverse */
  while (p > 0) {
    tmp[ti++] = hex[p & 0xF];
    p >>= 4;
  }

  /* Write to buffer */
  if (bufLen >= 3) {
    buf[len++] = '0';
    buf[len++] = 'x';
  }
  while (ti > 0 && len < bufLen - 1) {
    buf[len++] = tmp[--ti];
  }
  buf[len] = '\0';
  return len;
}

/*
 * Convert any Lua value to a string representation.
 * For non-stringifiable types (tables, functions, etc.), returns "typename:
 * 0xADDR" Returns the number of characters written (not including null
 * terminator).
 */
Size MLuaValueToStr(MLuaState *L, MLuaValue v, char *buf, Size bufLen) {
  UNUSED(L);

  if (bufLen == 0)
    return 0;

  /* nil */
  if (IsNil(v)) {
    if (bufLen >= 4) {
      buf[0] = 'n';
      buf[1] = 'i';
      buf[2] = 'l';
      buf[3] = '\0';
      return 3;
    }
    return 0;
  }

  /* true */
  if (IsTrue(v)) {
    if (bufLen >= 5) {
      buf[0] = 't';
      buf[1] = 'r';
      buf[2] = 'u';
      buf[3] = 'e';
      buf[4] = '\0';
      return 4;
    }
    return 0;
  }

  /* false */
  if (IsFalse(v)) {
    if (bufLen >= 6) {
      buf[0] = 'f';
      buf[1] = 'a';
      buf[2] = 'l';
      buf[3] = 's';
      buf[4] = 'e';
      buf[5] = '\0';
      return 5;
    }
    return 0;
  }

  /* Integer */
  if (IsInt(v)) {
    return MLuaIntToStr(MLuaGetIntVal(v), buf);
  }

  /* Float: NaN-boxed inline on 64-bit, a heap OBJTYPE_NUMBER on the 32-bit
   * tagging path. */
#if MLUA_PTR_SIZE == 8
  if (IsDouble(v)) {
    return MLuaDoubleToStr(GetDouble(v), buf, -1);
  }
#else
  if (IsPtr(v) &&
      MLUA_OBJTYPE((MLuaGCHeader *)GetPtr(v)) == OBJTYPE_NUMBER) {
    return MLuaDoubleToStr(MLuaToNumber(v), buf, -1);
  }
#endif

  /* Short string (inline) */
  if (IsShortStr(v)) {
    Size len = 0;
    char c0 = GetShortStrChar0(v);
    char c1 = GetShortStrChar1(v);
    char c2 = GetShortStrChar2(v);
    if (c0 && len < bufLen - 1)
      buf[len++] = c0;
    if (c1 && len < bufLen - 1)
      buf[len++] = c1;
    if (c2 && len < bufLen - 1)
      buf[len++] = c2;
    buf[len] = '\0';
    return len;
  }

  /* Light function */
  if (IsLightFunc(v)) {
    const char *prefix = "function: ";
    Size len = 0;
    Size i;
    for (i = 0; prefix[i] && len < bufLen - 1; i++) {
      buf[len++] = prefix[i];
    }
    len += WriteHexPtr((void *)GetLightFuncIndex(v), buf + len, bufLen - len);
    return len;
  }

  /* Heap objects */
  if (IsPtr(v)) {
    void *ptr = GetPtr(v);
    if (ptr == NULL) {
      if (bufLen >= 4) {
        buf[0] = 'n';
        buf[1] = 'i';
        buf[2] = 'l';
        buf[3] = '\0';
        return 3;
      }
      return 0;
    }

    MLuaGCHeader *h = (MLuaGCHeader *)ptr;
    U8 objType = MLUA_OBJTYPE(h);

    /* Heap string - return actual string content */
    if (objType == OBJTYPE_STRING) {
      const char *s = MLuaStringData(v);
      Size slen = MLuaStringLen(v);
      Size i;
      if (slen >= bufLen)
        slen = bufLen - 1;
      for (i = 0; i < slen; i++)
        buf[i] = s[i];
      buf[slen] = '\0';
      return slen;
    }

    /* Non-stringifiable types: "typename: 0xADDR" */
    const char *typeName;
    switch (objType) {
    case OBJTYPE_TABLE:
      typeName = "table: ";
      break;
    case OBJTYPE_FUNCTION:
      typeName = "function: ";
      break;
    case OBJTYPE_PROTO:
      typeName = "proto: ";
      break;
    case OBJTYPE_USERDATA:
      typeName = "userdata: ";
      break;
    case OBJTYPE_UPVALUE:
      typeName = "upvalue: ";
      break;
    case OBJTYPE_THREAD:
      typeName = "thread: ";
      break;
    default:
      typeName = "unknown: ";
      break;
    }

    Size len = 0;
    Size i;
    for (i = 0; typeName[i] && len < bufLen - 1; i++) {
      buf[len++] = typeName[i];
    }
    len += WriteHexPtr(ptr, buf + len, bufLen - len);
    return len;
  }

  /* Unknown type */
  if (bufLen >= 8) {
    const char *s = "unknown";
    Size i;
    for (i = 0; s[i] && i < bufLen - 1; i++)
      buf[i] = s[i];
    buf[i] = '\0';
    return i;
  }
  return 0;
}

/* ========================================================================== */
/* String to Number                                                           */
/* ========================================================================== */

Bool MLuaStrToNumber(const char *s, Size len, double *out) {
  const char *p = s;
  const char *end = s + len;
  double result = 0;
  double frac = 0.1;
  int expSign = 1;
  int exp = 0;
  Bool neg = FALSE;
  Bool hasDot = FALSE;
  Bool hasDigit = FALSE;
  Bool isHex = FALSE;

  /* Skip whitespace */
  while (p < end && (*p == ' ' || *p == '\t'))
    p++;
  if (p >= end)
    return FALSE;

  /* Sign */
  if (*p == '-') {
    neg = TRUE;
    p++;
  } else if (*p == '+')
    p++;

  /* Hex prefix */
  if (p + 1 < end && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
    isHex = TRUE;
    p += 2;
  }

  if (isHex) {
    /* Hex number */
    while (p < end) {
      char c = *p;
      int digit = -1;
      if (c >= '0' && c <= '9')
        digit = c - '0';
      else if (c >= 'a' && c <= 'f')
        digit = 10 + c - 'a';
      else if (c >= 'A' && c <= 'F')
        digit = 10 + c - 'A';
      else if (c == '.' && !hasDot) {
        hasDot = TRUE;
        p++;
        continue;
      } else
        break;

      if (hasDot) {
        result += digit * frac;
        frac /= 16.0;
      } else {
        result = result * 16 + digit;
      }
      hasDigit = TRUE;
      p++;
    }

    /* Hex exponent (p/P) */
    if (p < end && (*p == 'p' || *p == 'P')) {
      p++;
      if (p < end && *p == '-') {
        expSign = -1;
        p++;
      } else if (p < end && *p == '+')
        p++;

        while (p < end && *p >= '0' && *p <= '9') {
          if (exp < 1024) {
            exp = exp * 10 + (*p - '0');
            if (exp > 1024)
              exp = 1024;
          }
          p++;
        }
      exp *= expSign;

      /* Apply binary exponent */
      if (exp > 0) {
        while (exp-- > 0)
          result *= 2.0;
      } else {
        while (exp++ < 0)
          result /= 2.0;
      }
    }
  } else {
    /* Decimal number */
    while (p < end) {
      char c = *p;
      if (c >= '0' && c <= '9') {
        if (hasDot) {
          result += (c - '0') * frac;
          frac *= 0.1;
        } else {
          result = result * 10 + (c - '0');
        }
        hasDigit = TRUE;
      } else if (c == '.' && !hasDot) {
        hasDot = TRUE;
      } else {
        break;
      }
      p++;
    }

    /* Decimal exponent (e/E) */
    if (p < end && (*p == 'e' || *p == 'E')) {
      p++;
      if (p < end && *p == '-') {
        expSign = -1;
        p++;
      } else if (p < end && *p == '+')
        p++;

      while (p < end && *p >= '0' && *p <= '9') {
        if (exp < 308) {
          exp = exp * 10 + (*p - '0');
          if (exp > 308)
            exp = 308;
        }
        p++;
      }
      exp *= expSign;

      /* Apply decimal exponent */
      if (exp > 0) {
        while (exp-- > 0)
          result *= 10.0;
      } else {
        while (exp++ < 0)
          result /= 10.0;
      }
    }
  }

  /* Skip trailing whitespace */
  while (p < end && (*p == ' ' || *p == '\t'))
    p++;

  /* Must have consumed all input */
  if (!hasDigit || p != end)
    return FALSE;

  *out = neg ? -result : result;
  return TRUE;
}

/* ========================================================================== */
/* String to Integer                                                          */
/* ========================================================================== */

Bool MLuaStrToInt(const char *s, Size len, int base, I64 *out) {
  const char *p = s;
  const char *end = s + len;
  I64 result = 0;
  Bool neg = FALSE;

  /* Skip whitespace */
  while (p < end && (*p == ' ' || *p == '\t'))
    p++;
  if (p >= end)
    return FALSE;

  /* Sign */
  if (*p == '-') {
    neg = TRUE;
    p++;
  } else if (*p == '+')
    p++;

  /* Auto-detect base */
  if (base == 0) {
    if (p + 1 < end && p[0] == '0') {
      if (p[1] == 'x' || p[1] == 'X') {
        base = 16;
        p += 2;
      } else if (p[1] == 'o' || p[1] == 'O') {
        base = 8;
        p += 2;
      } else if (p[1] == 'b' || p[1] == 'B') {
        base = 2;
        p += 2;
      } else
        base = 10;
    } else {
      base = 10;
    }
  }

  /* Parse digits */
  Bool hasDigit = FALSE;
  while (p < end) {
    char c = *p;
    int digit = -1;

    if (c >= '0' && c <= '9')
      digit = c - '0';
    else if (c >= 'a' && c <= 'z')
      digit = 10 + c - 'a';
    else if (c >= 'A' && c <= 'Z')
      digit = 10 + c - 'A';
    else
      break;

    if (digit >= base)
      break;

    result = result * base + digit;
    hasDigit = TRUE;
    p++;
  }

  /* Skip trailing whitespace */
  while (p < end && (*p == ' ' || *p == '\t'))
    p++;

  if (!hasDigit || p != end)
    return FALSE;

  *out = neg ? -result : result;
  return TRUE;
}

/* ========================================================================== */
/* String Formatting                                                          */
/* ========================================================================== */

/*
 * Helper: Format unsigned integer to buffer in given base.
 * Returns number of characters written.
 */
static Size FmtUnsigned(U64 n, char *buf, Size bufLen, int base, Bool upper) {
  const char *hexL = "0123456789abcdef";
  const char *hexU = "0123456789ABCDEF";
  const char *digits = upper ? hexU : hexL;
  char tmp[32];
  int ti = 0;
  Size len = 0;

  if (n == 0) {
    if (bufLen > 0)
      buf[len++] = '0';
  } else {
    while (n > 0 && ti < 32) {
      tmp[ti++] = digits[n % base];
      n /= base;
    }
    while (ti > 0 && len < bufLen) {
      buf[len++] = tmp[--ti];
    }
  }
  return len;
}

Size MLuaFormat(MLuaState *L, const char *fmt, Size fmtLen, MLuaValue *args,
                int nargs, char *buf, Size bufLen) {
  Size out = 0;
  Size i = 0;
  int argIdx = 0;

  while (i < fmtLen && out < bufLen - 1) {
    if (fmt[i] == '%' && i + 1 < fmtLen) {
      char spec = fmt[i + 1];
      int width = 0;
      int prec = -1;
      Bool leftAlign = FALSE;
      Bool zeroPad = FALSE;
      Size j;

      i++;

      /* Parse flags */
      while (i < fmtLen) {
        if (fmt[i] == '-') {
          leftAlign = TRUE;
          i++;
        } else if (fmt[i] == '0') {
          zeroPad = TRUE;
          i++;
        } else if (fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#') {
          i++;
        } else
          break;
      }

      /* Parse width */
      while (i < fmtLen && fmt[i] >= '0' && fmt[i] <= '9') {
        if (width < 4096) {
          width = width * 10 + (fmt[i] - '0');
          if (width > 4096)
            width = 4096;
        }
        i++;
      }

      /* Parse precision */
      if (i < fmtLen && fmt[i] == '.') {
        prec = 0;
        i++;
        while (i < fmtLen && fmt[i] >= '0' && fmt[i] <= '9') {
          if (prec < 4096) {
            prec = prec * 10 + (fmt[i] - '0');
            if (prec > 4096)
              prec = 4096;
          }
          i++;
        }
      }

      if (i >= fmtLen)
        break;
      spec = fmt[i++];

      char tmp[128];
      Size tmpLen = 0;

      switch (spec) {
      case '%':
        tmp[0] = '%';
        tmpLen = 1;
        break;

      case 's': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          const char *s = MLuaStringData(v);
          Size slen = s ? MLuaStringLen(v) : 0;
          if (prec >= 0 && (Size)prec < slen)
            slen = (Size)prec;
          for (j = 0; j < slen && tmpLen < sizeof(tmp) - 1; j++) {
            tmp[tmpLen++] = s[j];
          }
        }
        break;
      }

      case 'd':
      case 'i': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          I64 n = IsInt(v) ? MLuaGetIntVal(v) : (I64)MLuaToNumber(v);
          tmpLen = MLuaIntToStr(n, tmp);
        }
        break;
      }

      case 'u': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          U64 n = IsInt(v) ? (U64)(U32)MLuaGetIntVal(v) : (U64)MLuaToNumber(v);
          tmpLen = FmtUnsigned(n, tmp, sizeof(tmp), 10, FALSE);
        }
        break;
      }

      case 'x':
      case 'X': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          U64 n = IsInt(v) ? (U64)(U32)MLuaGetIntVal(v) : (U64)MLuaToNumber(v);
          tmpLen = FmtUnsigned(n, tmp, sizeof(tmp), 16, spec == 'X');
        }
        break;
      }

      case 'o': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          U64 n = IsInt(v) ? (U64)(U32)MLuaGetIntVal(v) : (U64)MLuaToNumber(v);
          tmpLen = FmtUnsigned(n, tmp, sizeof(tmp), 8, FALSE);
        }
        break;
      }

      case 'f':
      case 'e':
      case 'E':
      case 'g':
      case 'G': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          double d = MLuaToNumber(v);
          if (prec < 0)
            prec = 6;
          tmpLen = MLuaDoubleToStr(d, tmp, prec);
        }
        break;
      }

      case 'c': {
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          I32 c = IsInt(v) ? MLuaGetIntVal(v) : (I32)MLuaToNumber(v);
          tmp[0] = (char)c;
          tmpLen = 1;
        }
        break;
      }

      case 'q': {
        /* Quoted string */
        if (argIdx < nargs) {
          MLuaValue v = args[argIdx++];
          const char *s = MLuaStringData(v);
          Size slen = s ? MLuaStringLen(v) : 0;
          tmp[tmpLen++] = '"';
          for (j = 0; j < slen && tmpLen < sizeof(tmp) - 2; j++) {
            char c = s[j];
            if (c == '"' || c == '\\' || c == '\n') {
              tmp[tmpLen++] = '\\';
              if (c == '\n')
                c = 'n';
            }
            tmp[tmpLen++] = c;
          }
          tmp[tmpLen++] = '"';
        }
        break;
      }

      default:
        tmp[0] = spec;
        tmpLen = 1;
        break;
      }

      /* Apply width padding */
      if (width > 0 && (Size)width > tmpLen) {
        Size pad = (Size)width - tmpLen;
        char padChar = zeroPad ? '0' : ' ';

        if (leftAlign) {
          for (j = 0; j < tmpLen && out < bufLen - 1; j++)
            buf[out++] = tmp[j];
          for (j = 0; j < pad && out < bufLen - 1; j++)
            buf[out++] = ' ';
        } else {
          for (j = 0; j < pad && out < bufLen - 1; j++)
            buf[out++] = padChar;
          for (j = 0; j < tmpLen && out < bufLen - 1; j++)
            buf[out++] = tmp[j];
        }
      } else {
        for (j = 0; j < tmpLen && out < bufLen - 1; j++)
          buf[out++] = tmp[j];
      }
    } else {
      buf[out++] = fmt[i++];
    }
  }

  buf[out] = '\0';
  return out;
}

/* ========================================================================== */
/* C Varargs Formatting                                                       */
/* ========================================================================== */

/*
 * Note: FmtUnsigned helper is defined above (shared with MLuaFormat)
 */

/*
 * Format a string using C varargs (va_list version).
 * Supports: %s, %d/%i, %u, %x/%X, %o, %c, %% (matches MLuaFormat specifiers).
 * Uses existing conversion functions where available.
 */
Size MLuaFormatVA(char *buf, Size bufLen, const char *fmt, va_list args) {
  char *out = buf;
  char *end = buf + bufLen - 1;
  const char *p = fmt;

  while (*p && out < end) {
    if (*p == '%' && p[1]) {
      p++;
      char spec = *p++;

      char tmp[64];
      Size tmpLen = 0;

      switch (spec) {
      case 's': {
        const char *s = va_arg(args, const char *);
        if (!s)
          s = "(null)";
        while (*s && out < end) {
          *out++ = *s++;
        }
        continue; /* Already wrote to output */
      }

      case 'd':
      case 'i': {
        int n = va_arg(args, int);
        tmpLen = MLuaIntToStr((I64)n, tmp);
        break;
      }

      case 'u': {
        unsigned int n = va_arg(args, unsigned int);
        tmpLen = FmtUnsigned((U64)n, tmp, sizeof(tmp), 10, FALSE);
        break;
      }

      case 'x': {
        unsigned int n = va_arg(args, unsigned int);
        tmpLen = FmtUnsigned((U64)n, tmp, sizeof(tmp), 16, FALSE);
        break;
      }

      case 'X': {
        unsigned int n = va_arg(args, unsigned int);
        tmpLen = FmtUnsigned((U64)n, tmp, sizeof(tmp), 16, TRUE);
        break;
      }

      case 'o': {
        unsigned int n = va_arg(args, unsigned int);
        tmpLen = FmtUnsigned((U64)n, tmp, sizeof(tmp), 8, FALSE);
        break;
      }

      case 'c': {
        int c = va_arg(args, int);
        tmp[0] = (char)c;
        tmpLen = 1;
        break;
      }

      case 'p': {
        void *ptr = va_arg(args, void *);
        if (out < end)
          *out++ = '0';
        if (out < end)
          *out++ = 'x';
        tmpLen = FmtUnsigned((UPtr)ptr, tmp, sizeof(tmp), 16, FALSE);
        break;
      }

      case '%':
        tmp[0] = '%';
        tmpLen = 1;
        break;

      default:
        /* Unknown specifier - copy as-is */
        tmp[0] = '%';
        tmp[1] = spec;
        tmpLen = 2;
        break;
      }

      /* Copy tmp to output */
      Size j;
      for (j = 0; j < tmpLen && out < end; j++) {
        *out++ = tmp[j];
      }
    } else {
      *out++ = *p++;
    }
  }

  *out = '\0';
  return (Size)(out - buf);
}
