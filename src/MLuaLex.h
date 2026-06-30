/*
 * MicroLua - MLuaLex.h
 * Pull-based lexer for Lua source code
 */

#ifndef MLUA_LEX_H
#define MLUA_LEX_H

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaValue.h"

/* ========================================================================== */
/* Token Types                                                                */
/* ========================================================================== */

typedef enum {
  /* End of file */
  TK_EOF = 0,

  /* Literals */
  TK_NUMBER, /* Number literal */
  TK_STRING, /* String literal */
  TK_NAME,   /* Identifier */

  /* Keywords (alphabetically sorted for binary search) */
  TK_AND,
  TK_BREAK,
  TK_DO,
  TK_ELSE,
  TK_ELSEIF,
  TK_END,
  TK_FALSE,
  TK_FOR,
  TK_FUNCTION,
  TK_IF,
  TK_IN,
  TK_LOCAL,
  TK_NIL,
  TK_NOT,
  TK_OR,
  TK_REPEAT,
  TK_RETURN,
  TK_THEN,
  TK_TRUE,
  TK_UNTIL,
  TK_WHILE,

  /* Operators */
  TK_PLUS,    /* + */
  TK_MINUS,   /* - */
  TK_STAR,    /* * */
  TK_SLASH,   /* / */
  TK_PERCENT, /* % */
  TK_CARET,   /* ^ */
  TK_HASH,    /* # */
  TK_EQ,      /* == */
  TK_NE,      /* ~= */
  TK_LE,      /* <= */
  TK_GE,      /* >= */
  TK_LT,      /* < */
  TK_GT,      /* > */
  TK_ASSIGN,  /* = */
  TK_CONCAT,  /* .. */
  TK_DOTS,    /* ... */

  /* Delimiters */
  TK_LPAREN,    /* ( */
  TK_RPAREN,    /* ) */
  TK_LBRACE,    /* { */
  TK_RBRACE,    /* } */
  TK_LBRACKET,  /* [ */
  TK_RBRACKET,  /* ] */
  TK_SEMICOLON, /* ; */
  TK_COLON,     /* : */
  TK_COMMA,     /* , */
  TK_DOT,       /* . */

  /* Error token */
  TK_ERROR
} MLuaTokenType;

/* ========================================================================== */
/* Token Structure                                                            */
/* ========================================================================== */

typedef struct {
  MLuaTokenType Type;
  Size Line;   /* Line number (1-indexed) */
  Size Column; /* Column number (1-indexed) */

  /* Token value (depends on type) */
  union {
    double Number; /* For TK_NUMBER */
    struct {
      const char *Data;
      Size Length;
    } String; /* For TK_STRING and TK_NAME */
  } Value;
} MLuaToken;

/* ========================================================================== */
/* Lexer State                                                                */
/* ========================================================================== */

typedef struct {
  MLuaState *L; /* Runtime state (for string interning) */

  /* Source */
  const char *Source;  /* Source code pointer */
  const char *Current; /* Current position in source */
  const char *End;     /* End of source */

  /* Position tracking */
  Size Line;             /* Current line number */
  Size Column;           /* Current column number */
  const char *LineStart; /* Start of current line */

  /* Current token */
  MLuaToken Token; /* Most recently lexed token */

  /* Lookahead */
  MLuaToken Lookahead; /* Lookahead token (for parser) */
  Bool HasLookahead;   /* Is lookahead valid? */

  /* Error state */
  const char *Error; /* Error message, or NULL */

  /* String buffer for building tokens */
  char *Buffer;   /* Temporary buffer for strings */
  Size BufferCap; /* Buffer capacity */
  Size BufferLen; /* Current length in buffer */
} MLuaLexer;

/* ========================================================================== */
/* Lexer API                                                                  */
/* ========================================================================== */

/*
 * Initialize a lexer for a source string.
 */
void MLuaLexInit(MLuaLexer *lex, MLuaState *L, const char *source, Size length);

/*
 * Get the next token.
 * Returns the token type, and fills lex->Token with details.
 */
MLuaTokenType MLuaLexNext(MLuaLexer *lex);

/*
 * Peek at the next token without consuming it.
 */
MLuaTokenType MLuaLexPeek(MLuaLexer *lex);

/*
 * Check if current token is of given type.
 */
Bool MLuaLexCheck(MLuaLexer *lex, MLuaTokenType type);

/*
 * Consume token if it matches type, otherwise error.
 */
Bool MLuaLexExpect(MLuaLexer *lex, MLuaTokenType type);

/*
 * Get a human-readable name for a token type.
 */
const char *MLuaTokenName(MLuaTokenType type);

#endif /* MLUA_LEX_H */
