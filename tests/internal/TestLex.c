/*
 * MicroLua - TestLex.c
 * Tests for MLuaLex.c (lexer)
 */

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaLex.h"
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

#define ASSERT_EQ(a, b) ASSERT((a) == (b))
#define ASSERT_NE(a, b) ASSERT((a) != (b))

#define TEST_HEAP_SIZE (64 * 1024)
static U8 TestHeap[TEST_HEAP_SIZE] __attribute__((aligned(8)));

/* ========================================================================== */
/* Token Tests                                                                */
/* ========================================================================== */

TEST(Keywords) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "if then else end while do for in";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_IF);
  ASSERT_EQ(MLuaLexNext(&lex), TK_THEN);
  ASSERT_EQ(MLuaLexNext(&lex), TK_ELSE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_END);
  ASSERT_EQ(MLuaLexNext(&lex), TK_WHILE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_DO);
  ASSERT_EQ(MLuaLexNext(&lex), TK_FOR);
  ASSERT_EQ(MLuaLexNext(&lex), TK_IN);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Numbers) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "123 45.67 0xFF 1e10";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_NUMBER);
  ASSERT_EQ((int)lex.Token.Value.Number, 123);

  ASSERT_EQ(MLuaLexNext(&lex), TK_NUMBER);
  /* Check approximately */
  ASSERT(lex.Token.Value.Number > 45.0 && lex.Token.Value.Number < 46.0);

  ASSERT_EQ(MLuaLexNext(&lex), TK_NUMBER);
  ASSERT_EQ((int)lex.Token.Value.Number, 255);

  ASSERT_EQ(MLuaLexNext(&lex), TK_NUMBER);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Strings) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "\"hello\" 'world'";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_STRING);
  ASSERT_EQ(lex.Token.Value.String.Length, 5);

  ASSERT_EQ(MLuaLexNext(&lex), TK_STRING);
  ASSERT_EQ(lex.Token.Value.String.Length, 5);

  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Operators) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "+ - * / == ~= <= >= < > ..";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_PLUS);
  ASSERT_EQ(MLuaLexNext(&lex), TK_MINUS);
  ASSERT_EQ(MLuaLexNext(&lex), TK_STAR);
  ASSERT_EQ(MLuaLexNext(&lex), TK_SLASH);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EQ);
  ASSERT_EQ(MLuaLexNext(&lex), TK_NE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_LE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_GE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_LT);
  ASSERT_EQ(MLuaLexNext(&lex), TK_GT);
  ASSERT_EQ(MLuaLexNext(&lex), TK_CONCAT);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Comments) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "a -- comment\nb --[[ long\ncomment ]]c";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME);
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME); /* b */
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME); /* c */
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Identifiers) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "foo _bar baz123 _";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME);
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME);
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME);
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(Delimiters) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  const char *src = "(){}[];:,.";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_LPAREN);
  ASSERT_EQ(MLuaLexNext(&lex), TK_RPAREN);
  ASSERT_EQ(MLuaLexNext(&lex), TK_LBRACE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_RBRACE);
  ASSERT_EQ(MLuaLexNext(&lex), TK_LBRACKET);
  ASSERT_EQ(MLuaLexNext(&lex), TK_RBRACKET);
  ASSERT_EQ(MLuaLexNext(&lex), TK_SEMICOLON);
  ASSERT_EQ(MLuaLexNext(&lex), TK_COLON);
  ASSERT_EQ(MLuaLexNext(&lex), TK_COMMA);
  ASSERT_EQ(MLuaLexNext(&lex), TK_DOT);
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

TEST(UTF8Identifiers) {
  MLuaState *L = MLuaStateInit(TestHeap, TEST_HEAP_SIZE);
  MLuaLexer lex;
  /* UTF-8: "日本語" (Japanese), "变量" (Chinese), "переменная" (Russian) */
  const char *src =
      "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E \xE5\x8F\x98\xE9\x87\x8F "
      "\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xBD\xD0\xB0"
      "\xD1\x8F";

  MLuaLexInit(&lex, L, src, StrLen(src));

  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME); /* 日本語 */
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME); /* 变量 */
  ASSERT_EQ(MLuaLexNext(&lex), TK_NAME); /* переменная */
  ASSERT_EQ(MLuaLexNext(&lex), TK_EOF);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
  printf("MicroLua Lexer Tests\n");
  printf("====================\n\n");

  printf("Token Types:\n");
  RUN_TEST(Keywords);
  RUN_TEST(Numbers);
  RUN_TEST(Strings);
  RUN_TEST(Operators);
  RUN_TEST(Comments);
  RUN_TEST(Identifiers);
  RUN_TEST(Delimiters);
  RUN_TEST(UTF8Identifiers);

  printf("\n====================\n");
  printf("Results: %d passed, %d failed\n", TestsPassed, TestsFailed);

  return TestsFailed > 0 ? 1 : 0;
}
