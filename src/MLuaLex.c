/*
 * MicroLua - MLuaLex.c
 * Pull-based lexer implementation with UTF-8 support
 */

#include "MLuaLex.h"
#include "MLuaString.h"
#include "MLuaUTF8.h"

/* ========================================================================== */
/* Character Classification                                                   */
/* ========================================================================== */

static Bool IsDigit(char c) { return c >= '0' && c <= '9'; }

/*
 * Check if byte is a UTF-8 continuation byte (10xxxxxx).
 * These bytes are valid inside identifiers after the first character.
 */
static Bool IsUTF8Continuation(U8 c) { return (c & 0xC0) == 0x80; }

/*
 * Check if byte starts a valid identifier.
 * Includes: ASCII letters, underscore, and UTF-8 lead bytes (for Unicode
 * letters).
 */
static Bool IsAlpha(char c) {
  U8 u = (U8)c;
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
    return TRUE;
  }
  /* UTF-8 lead bytes (11xxxxxx) can start Unicode identifiers */
  if ((u & 0xC0) == 0xC0) {
    return TRUE;
  }
  return FALSE;
}

/*
 * Check if byte can continue an identifier.
 * Includes: alphanumerics, underscore, UTF-8 lead bytes, and continuation
 * bytes.
 */
static Bool IsAlnum(char c) {
  U8 u = (U8)c;
  if (IsAlpha(c) || IsDigit(c)) {
    return TRUE;
  }
  /* UTF-8 continuation bytes can appear in the middle of identifiers */
  if (IsUTF8Continuation(u)) {
    return TRUE;
  }
  return FALSE;
}

static Bool IsSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static Bool IsHexDigit(char c) {
  return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* ========================================================================== */
/* Keyword Table                                                              */
/* ========================================================================== */

typedef struct {
  const char *Name;
  MLuaTokenType Type;
} Keyword;

static const Keyword Keywords[] = {
    {"and", TK_AND},       {"break", TK_BREAK},   {"do", TK_DO},
    {"else", TK_ELSE},     {"elseif", TK_ELSEIF}, {"end", TK_END},
    {"false", TK_FALSE},   {"for", TK_FOR},       {"function", TK_FUNCTION},
    {"if", TK_IF},         {"in", TK_IN},         {"local", TK_LOCAL},
    {"nil", TK_NIL},       {"not", TK_NOT},       {"or", TK_OR},
    {"repeat", TK_REPEAT}, {"return", TK_RETURN}, {"then", TK_THEN},
    {"true", TK_TRUE},     {"until", TK_UNTIL},   {"while", TK_WHILE},
    {NULL, TK_EOF} /* Sentinel */
};

static MLuaTokenType LookupKeyword(const char *name, Size len) {
  const Keyword *kw;

  for (kw = Keywords; kw->Name != NULL; kw++) {
    Size kwLen = StrLen(kw->Name);
    if (kwLen == len && MemCmp(kw->Name, name, len) == 0) {
      return kw->Type;
    }
  }

  return TK_NAME; /* Not a keyword, it's an identifier */
}

/* ========================================================================== */
/* Lexer Helpers                                                              */
/* ========================================================================== */

static char Peek(MLuaLexer *lex) {
  if (lex->Current >= lex->End) {
    return '\0';
  }
  return *lex->Current;
}

static char PeekNext(MLuaLexer *lex) {
  if (lex->Current + 1 >= lex->End) {
    return '\0';
  }
  return lex->Current[1];
}

static char Advance(MLuaLexer *lex) {
  char c;

  if (lex->Current >= lex->End) {
    return '\0';
  }

  c = *lex->Current++;
  lex->Column++;

  if (c == '\n') {
    lex->Line++;
    lex->Column = 1;
    lex->LineStart = lex->Current;
  }

  return c;
}

static Bool Match(MLuaLexer *lex, char expected) {
  if (Peek(lex) == expected) {
    Advance(lex);
    return TRUE;
  }
  return FALSE;
}

static void SkipWhitespace(MLuaLexer *lex) {
  for (;;) {
    char c = Peek(lex);

    if (IsSpace(c)) {
      Advance(lex);
    } else if (c == '-' && PeekNext(lex) == '-') {
      /* Comment */
      Advance(lex);
      Advance(lex);

      /* Check for long comment */
      if (Peek(lex) == '[' && (PeekNext(lex) == '[' || PeekNext(lex) == '=')) {
        /* Long comment - count level (number of = signs) */
        int level = 0;
        Advance(lex); /* Skip [ */
        while (Peek(lex) == '=') {
          level++;
          Advance(lex);
        }
        if (Peek(lex) == '[') {
          Advance(lex); /* Skip second [ */
          /* Skip until matching close bracket ]=...=] */
          while (lex->Current < lex->End) {
            if (Peek(lex) == ']') {
              Advance(lex);
              int closeLevel = 0;
              while (Peek(lex) == '=' && closeLevel < level) {
                closeLevel++;
                Advance(lex);
              }
              if (closeLevel == level && Peek(lex) == ']') {
                Advance(lex);
                break; /* Found matching close */
              }
            } else {
              Advance(lex);
            }
          }
        }
      } else {
        /* Short comment - skip to end of line */
        while (Peek(lex) != '\n' && Peek(lex) != '\0') {
          Advance(lex);
        }
      }
    } else {
      break;
    }
  }
}

static void SetError(MLuaLexer *lex, const char *msg) {
  lex->Error = msg;
  lex->Token.Type = TK_ERROR;
}

/* ========================================================================== */
/* Token Scanning                                                             */
/* ========================================================================== */

static void ScanNumber(MLuaLexer *lex) {
  const char *start = lex->Current - 1; /* Already consumed first digit */
  Bool isHex = FALSE;
  Bool hasDecimal = FALSE;
  Bool hasExp = FALSE;

  /* Check for hex */
  if (*start == '0' && (Peek(lex) == 'x' || Peek(lex) == 'X')) {
    isHex = TRUE;
    Advance(lex);

    while (IsHexDigit(Peek(lex))) {
      Advance(lex);
    }
  } else {
    /* Decimal number */
    while (IsDigit(Peek(lex))) {
      Advance(lex);
    }

    /* Decimal point */
    if (Peek(lex) == '.' && IsDigit(PeekNext(lex))) {
      hasDecimal = TRUE;
      Advance(lex); /* Consume '.' */

      while (IsDigit(Peek(lex))) {
        Advance(lex);
      }
    }

    /* Exponent */
    if (Peek(lex) == 'e' || Peek(lex) == 'E') {
      hasExp = TRUE;
      Advance(lex);

      if (Peek(lex) == '+' || Peek(lex) == '-') {
        Advance(lex);
      }

      if (!IsDigit(Peek(lex))) {
        SetError(lex, "invalid number: expected exponent digits");
        return;
      }

      while (IsDigit(Peek(lex))) {
        Advance(lex);
      }
    }
  }

  /* Parse the number */
  UNUSED(hasDecimal);
  UNUSED(hasExp);

  /* Simple parsing - atof equivalent */
  {
    double value = 0.0;
    const char *p = start;

    if (isHex) {
      p += 2; /* Skip 0x */
      while (p < lex->Current) {
        char c = *p++;
        int digit;
        if (c >= '0' && c <= '9')
          digit = c - '0';
        else if (c >= 'a' && c <= 'f')
          digit = 10 + c - 'a';
        else if (c >= 'A' && c <= 'F')
          digit = 10 + c - 'A';
        else
          break;
        value = value * 16.0 + digit;
      }
    } else {
      double frac = 0.0;
      double fracMult = 0.1;
      int exp = 0;
      int expSign = 1;
      Bool inFrac = FALSE;
      Bool inExp = FALSE;

      while (p < lex->Current) {
        char c = *p++;
        if (c == '.') {
          inFrac = TRUE;
        } else if (c == 'e' || c == 'E') {
          inExp = TRUE;
        } else if (c == '+') {
          /* Skip */
        } else if (c == '-') {
          expSign = -1;
        } else if (IsDigit(c)) {
          if (inExp) {
            exp = exp * 10 + (c - '0');
          } else if (inFrac) {
            frac += (c - '0') * fracMult;
            fracMult *= 0.1;
          } else {
            value = value * 10.0 + (c - '0');
          }
        }
      }

      value += frac;

      /* Apply exponent */
      if (inExp) {
        int i;
        double mult = 1.0;
        for (i = 0; i < exp; i++) {
          mult *= 10.0;
        }
        if (expSign > 0) {
          value *= mult;
        } else {
          value /= mult;
        }
      }
    }

    lex->Token.Value.Number = value;
  }

  lex->Token.Type = TK_NUMBER;
}

/* Map an escape character to its decoded byte */
static char DecodeEscape(char c) {
  switch (c) {
  case 'n':
    return '\n';
  case 't':
    return '\t';
  case 'r':
    return '\r';
  case '0':
    return '\0';
  case 'a':
    return '\a';
  case 'b':
    return '\b';
  case 'f':
    return '\f';
  case 'v':
    return '\v';
  default:
    return c; /* \\ \" \' decode to themselves */
  }
}

static void ScanString(MLuaLexer *lex, char quote) {
  const char *start = lex->Current;
  Bool hasEscape = FALSE;

  while (Peek(lex) != quote && Peek(lex) != '\0') {
    if (Peek(lex) == '\n') {
      SetError(lex, "unfinished string");
      return;
    }

    if (Peek(lex) == '\\') {
      Advance(lex); /* Skip backslash */
      /* Validate escape sequences */
      switch (Peek(lex)) {
      case 'n':
      case 't':
      case 'r':
      case '\\':
      case '"':
      case '\'':
      case '0':
      case 'a':
      case 'b':
      case 'f':
      case 'v':
        hasEscape = TRUE;
        break;
      default:
        SetError(lex, "invalid escape sequence");
        return;
      }
    }

    Advance(lex);
  }

  if (Peek(lex) != quote) {
    SetError(lex, "unfinished string");
    return;
  }

  Advance(lex); /* Consume closing quote */

  lex->Token.Type = TK_STRING;

  if (!hasEscape) {
    /* Zero-copy: point straight into the source */
    lex->Token.Value.String.Data = start;
    lex->Token.Value.String.Length = (Size)(lex->Current - start - 1);
    return;
  }

  /* Escapes present: decode into a heap buffer (shorter than the raw span) */
  {
    Size rawLen = (Size)(lex->Current - start - 1);
    char *buf = (char *)MLuaAlloc(lex->L, rawLen);
    Size out = 0;
    Size i;

    if (!buf) {
      SetError(lex, "out of memory");
      return;
    }

    for (i = 0; i < rawLen; i++) {
      if (start[i] == '\\' && i + 1 < rawLen) {
        i++;
        buf[out++] = DecodeEscape(start[i]);
      } else {
        buf[out++] = start[i];
      }
    }

    lex->Token.Value.String.Data = buf;
    lex->Token.Value.String.Length = out;
  }
}

static void ScanLongString(MLuaLexer *lex) {
  const char *start;
  int level = 0;

  /* Count equals signs */
  while (Peek(lex) == '=') {
    level++;
    Advance(lex);
  }

  if (Peek(lex) != '[') {
    SetError(lex, "invalid long string delimiter");
    return;
  }
  Advance(lex); /* Skip second [ */

  /* Skip initial newline if present */
  if (Peek(lex) == '\n') {
    Advance(lex);
  }

  start = lex->Current;

  /* Find closing delimiter */
  while (lex->Current < lex->End) {
    if (Peek(lex) == ']') {
      const char *closeStart = lex->Current;
      int closeLevel = 0;

      Advance(lex);
      while (Peek(lex) == '=') {
        closeLevel++;
        Advance(lex);
      }

      if (Peek(lex) == ']' && closeLevel == level) {
        lex->Token.Type = TK_STRING;
        lex->Token.Value.String.Data = start;
        lex->Token.Value.String.Length = (Size)(closeStart - start);
        Advance(lex); /* Skip final ] */
        return;
      }
    } else {
      Advance(lex);
    }
  }

  SetError(lex, "unfinished long string");
}

static void ScanName(MLuaLexer *lex) {
  const char *start = lex->Current - 1; /* Already consumed first char */

  while (IsAlnum(Peek(lex))) {
    Advance(lex);
  }

  Size len = (Size)(lex->Current - start);
  lex->Token.Type = LookupKeyword(start, len);

  if (lex->Token.Type == TK_NAME) {
    lex->Token.Value.String.Data = start;
    lex->Token.Value.String.Length = len;
  }
}

/* ========================================================================== */
/* Main Lexer Functions                                                       */
/* ========================================================================== */

void MLuaLexInit(MLuaLexer *lex, MLuaState *L, const char *source,
                 Size length) {
  MemSet(lex, 0, sizeof(MLuaLexer));

  lex->L = L;
  lex->Source = source;
  lex->Current = source;
  lex->End = source + length;
  lex->Line = 1;
  lex->Column = 1;
  lex->LineStart = source;
  lex->HasLookahead = FALSE;
  lex->Error = NULL;
}

MLuaTokenType MLuaLexNext(MLuaLexer *lex) {
  char c;

  /* Use lookahead if available */
  if (lex->HasLookahead) {
    lex->Token = lex->Lookahead;
    lex->HasLookahead = FALSE;
    return lex->Token.Type;
  }

  SkipWhitespace(lex);

  lex->Token.Line = lex->Line;
  lex->Token.Column = lex->Column;

  if (lex->Current >= lex->End) {
    lex->Token.Type = TK_EOF;
    return TK_EOF;
  }

  c = Advance(lex);

  /* Single character tokens */
  switch (c) {
  case '(':
    lex->Token.Type = TK_LPAREN;
    return TK_LPAREN;
  case ')':
    lex->Token.Type = TK_RPAREN;
    return TK_RPAREN;
  case '{':
    lex->Token.Type = TK_LBRACE;
    return TK_LBRACE;
  case '}':
    lex->Token.Type = TK_RBRACE;
    return TK_RBRACE;
  case ']':
    lex->Token.Type = TK_RBRACKET;
    return TK_RBRACKET;
  case ';':
    lex->Token.Type = TK_SEMICOLON;
    return TK_SEMICOLON;
  case ':':
    lex->Token.Type = TK_COLON;
    return TK_COLON;
  case ',':
    lex->Token.Type = TK_COMMA;
    return TK_COMMA;
  case '+':
    lex->Token.Type = TK_PLUS;
    return TK_PLUS;
  case '-':
    lex->Token.Type = TK_MINUS;
    return TK_MINUS;
  case '*':
    lex->Token.Type = TK_STAR;
    return TK_STAR;
  case '/':
    lex->Token.Type = TK_SLASH;
    return TK_SLASH;
  case '%':
    lex->Token.Type = TK_PERCENT;
    return TK_PERCENT;
  case '^':
    lex->Token.Type = TK_CARET;
    return TK_CARET;
  case '#':
    lex->Token.Type = TK_HASH;
    return TK_HASH;

  case '[':
    if (Peek(lex) == '[' || Peek(lex) == '=') {
      ScanLongString(lex);
      return lex->Token.Type;
    }
    lex->Token.Type = TK_LBRACKET;
    return TK_LBRACKET;

  case '=':
    if (Match(lex, '=')) {
      lex->Token.Type = TK_EQ;
      return TK_EQ;
    }
    lex->Token.Type = TK_ASSIGN;
    return TK_ASSIGN;

  case '~':
    if (Match(lex, '=')) {
      lex->Token.Type = TK_NE;
      return TK_NE;
    }
    SetError(lex, "unexpected character '~'");
    return TK_ERROR;

  case '<':
    if (Match(lex, '=')) {
      lex->Token.Type = TK_LE;
      return TK_LE;
    }
    lex->Token.Type = TK_LT;
    return TK_LT;

  case '>':
    if (Match(lex, '=')) {
      lex->Token.Type = TK_GE;
      return TK_GE;
    }
    lex->Token.Type = TK_GT;
    return TK_GT;

  case '.':
    if (Match(lex, '.')) {
      if (Match(lex, '.')) {
        lex->Token.Type = TK_DOTS;
        return TK_DOTS;
      }
      lex->Token.Type = TK_CONCAT;
      return TK_CONCAT;
    }
    if (IsDigit(Peek(lex))) {
      /* Number starting with . */
      ScanNumber(lex);
      return lex->Token.Type;
    }
    lex->Token.Type = TK_DOT;
    return TK_DOT;

  case '"':
  case '\'':
    ScanString(lex, c);
    return lex->Token.Type;

  default:
    if (IsDigit(c)) {
      ScanNumber(lex);
      return lex->Token.Type;
    }
    if (IsAlpha(c)) {
      ScanName(lex);
      return lex->Token.Type;
    }
    SetError(lex, "unexpected character");
    return TK_ERROR;
  }
}

MLuaTokenType MLuaLexPeek(MLuaLexer *lex) {
  if (!lex->HasLookahead) {
    MLuaToken saved = lex->Token;
    MLuaLexNext(lex);
    lex->Lookahead = lex->Token;
    lex->Token = saved;
    lex->HasLookahead = TRUE;
  }
  return lex->Lookahead.Type;
}

Bool MLuaLexCheck(MLuaLexer *lex, MLuaTokenType type) {
  return lex->Token.Type == type;
}

Bool MLuaLexExpect(MLuaLexer *lex, MLuaTokenType type) {
  if (!MLuaLexCheck(lex, type)) {
    SetError(lex, "unexpected token");
    return FALSE;
  }
  MLuaLexNext(lex);
  return TRUE;
}

/* ========================================================================== */
/* Token Names                                                                */
/* ========================================================================== */

const char *MLuaTokenName(MLuaTokenType type) {
  switch (type) {
  case TK_EOF:
    return "end of file";
  case TK_NUMBER:
    return "number";
  case TK_STRING:
    return "string";
  case TK_NAME:
    return "identifier";
  case TK_AND:
    return "'and'";
  case TK_BREAK:
    return "'break'";
  case TK_DO:
    return "'do'";
  case TK_ELSE:
    return "'else'";
  case TK_ELSEIF:
    return "'elseif'";
  case TK_END:
    return "'end'";
  case TK_FALSE:
    return "'false'";
  case TK_FOR:
    return "'for'";
  case TK_FUNCTION:
    return "'function'";
  case TK_IF:
    return "'if'";
  case TK_IN:
    return "'in'";
  case TK_LOCAL:
    return "'local'";
  case TK_NIL:
    return "'nil'";
  case TK_NOT:
    return "'not'";
  case TK_OR:
    return "'or'";
  case TK_REPEAT:
    return "'repeat'";
  case TK_RETURN:
    return "'return'";
  case TK_THEN:
    return "'then'";
  case TK_TRUE:
    return "'true'";
  case TK_UNTIL:
    return "'until'";
  case TK_WHILE:
    return "'while'";
  case TK_PLUS:
    return "'+'";
  case TK_MINUS:
    return "'-'";
  case TK_STAR:
    return "'*'";
  case TK_SLASH:
    return "'/'";
  case TK_PERCENT:
    return "'%'";
  case TK_CARET:
    return "'^'";
  case TK_HASH:
    return "'#'";
  case TK_EQ:
    return "'=='";
  case TK_NE:
    return "'~='";
  case TK_LE:
    return "'<='";
  case TK_GE:
    return "'>='";
  case TK_LT:
    return "'<'";
  case TK_GT:
    return "'>'";
  case TK_ASSIGN:
    return "'='";
  case TK_CONCAT:
    return "'..'";
  case TK_DOTS:
    return "'...'";
  case TK_LPAREN:
    return "'('";
  case TK_RPAREN:
    return "')'";
  case TK_LBRACE:
    return "'{'";
  case TK_RBRACE:
    return "'}'";
  case TK_LBRACKET:
    return "'['";
  case TK_RBRACKET:
    return "']'";
  case TK_SEMICOLON:
    return "';'";
  case TK_COLON:
    return "':'";
  case TK_COMMA:
    return "','";
  case TK_DOT:
    return "'.'";
  case TK_ERROR:
    return "error";
  default:
    return "unknown";
  }
}
