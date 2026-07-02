/*
 * MicroLua on the TI-84 Plus CE - interactive REPL (compiler builds only).
 *
 * One line = one chunk, matching the core REPL semantics: locals do not
 * persist across lines, globals do (the state lives for the session).
 *
 * Keyboard mapping (os_GetKey, so 2nd/alpha work normally):
 *   - alpha letters type LOWERCASE (Lua keywords are lowercase);
 *     the OS lowercase-alpha mode types uppercase instead
 *   - sto-> types '=', 2nd+math (TEST menu) gives == ~= < > <= >=
 *   - enter runs the line; del deletes; clear empties the line,
 *     and on an empty line leaves the REPL
 */

#include <ti/getkey.h>
#include <ti/screen.h>

#include "mlua_ce.h"

#define REPL_LINE_MAX 96
#define REPL_VIEW_COLS 24 /* 26-column home screen minus the "> " prompt */

/* Translate an os_GetKey code to ASCII text (0-2 chars). Returns the number
 * of chars written to out. */
static unsigned char KeyToText(unsigned short k, char out[2]) {
  if (k >= k_CapA && k <= k_CapZ) {
    out[0] = (char)('a' + (k - k_CapA));
    return 1;
  }
  if (k >= k_La && ((k - k_La) & 0xFF) == 0 && (k - k_La) >> 8 <= 25) {
    out[0] = (char)('A' + ((k - k_La) >> 8));
    return 1;
  }
  if (k >= k_0 && k <= k_9) {
    out[0] = (char)('0' + (k - k_0));
    return 1;
  }
  switch (k) {
  case k_Add:
    out[0] = '+';
    return 1;
  case k_Sub:
    out[0] = '-';
    return 1;
  case k_Mul:
    out[0] = '*';
    return 1;
  case k_Div:
    out[0] = '/';
    return 1;
  case k_Expon:
    out[0] = '^';
    return 1;
  case k_LParen:
    out[0] = '(';
    return 1;
  case k_RParen:
    out[0] = ')';
    return 1;
  case k_LBrack:
    out[0] = '[';
    return 1;
  case k_RBrack:
    out[0] = ']';
    return 1;
  case k_LBrace:
    out[0] = '{';
    return 1;
  case k_RBrace:
    out[0] = '}';
    return 1;
  case k_Comma:
    out[0] = ',';
    return 1;
  case k_DecPnt:
    out[0] = '.';
    return 1;
  case k_Space:
    out[0] = ' ';
    return 1;
  case k_Colon:
    out[0] = ':';
    return 1;
  case k_Quote:
    out[0] = '"';
    return 1;
  case k_Quest:
    out[0] = '?';
    return 1;
  case k_Store:
    out[0] = '=';
    return 1;
  case k_Chs:
    out[0] = '-';
    return 1;
  case k_Tequ:
    out[0] = '=';
    out[1] = '=';
    return 2;
  case k_TNoteQ:
    out[0] = '~';
    out[1] = '=';
    return 2;
  case k_TGT:
    out[0] = '>';
    return 1;
  case k_TLT:
    out[0] = '<';
    return 1;
  case k_TGTE:
    out[0] = '>';
    out[1] = '=';
    return 2;
  case k_TLTE:
    out[0] = '<';
    out[1] = '=';
    return 2;
  default:
    return 0;
  }
}

/* Redraw the edit row: prompt plus the tail of the buffer that fits. */
static void DrawLine(unsigned char row, const char *buf, Size len) {
  char view[REPL_VIEW_COLS + 1];
  Size start = len > REPL_VIEW_COLS ? len - REPL_VIEW_COLS : 0;
  Size n = len - start;
  Size i;
  for (i = 0; i < n; i++) {
    view[i] = buf[start + i];
  }
  while (i < REPL_VIEW_COLS) {
    view[i++] = ' ';
  }
  view[REPL_VIEW_COLS] = '\0';
  os_SetCursorPos(row, 0);
  os_PutStrFull("> ");
  os_PutStrFull(view);
  os_SetCursorPos(row, (unsigned char)(2 + n));
}

/* Read one line; returns FALSE when the user leaves the REPL (clear on an
 * empty line). */
static Bool ReadLine(char *buf, Size *lenOut) {
  unsigned int row, col;
  Size len = 0;
  os_GetCursorPos(&row, &col);
  if (row > 8) {
    os_NewLine(); /* let the OS scroll, then re-query */
    os_GetCursorPos(&row, &col);
  }
  for (;;) {
    unsigned short key;
    char text[2];
    unsigned char n;
    DrawLine((unsigned char)row, buf, len);
    key = os_GetKey();
    switch (key) {
    case k_Enter:
      os_NewLine();
      *lenOut = len;
      return TRUE;
    case k_Del:
      if (len > 0) {
        len--;
      }
      break;
    case k_Clear:
      if (len == 0) {
        return FALSE;
      }
      len = 0;
      break;
    default:
      n = KeyToText(key, text);
      if (n >= 1 && len < REPL_LINE_MAX) {
        buf[len++] = text[0];
      }
      if (n == 2 && len < REPL_LINE_MAX) {
        buf[len++] = text[1];
      }
      break;
    }
  }
}

void MLuaCeRepl(void) {
  static char line[REPL_LINE_MAX];
  MLuaState *L;

  MLuaCeConsoleInit();
  L = MLuaCeNewState();
  if (!L) {
    MLuaCeConsoleWriteStr("E: state creation failed\n");
    MLuaCeConsolePause();
    return;
  }
  MLuaCeConsoleWriteStr("MicroLua REPL\n");
  MLuaCeConsoleWriteStr("clear on empty line: exit\n");

  for (;;) {
    Size len;
    if (!ReadLine(line, &len)) {
      MLuaCeGfxCleanup(); /* in case a REPL line left gfx mode active */
      os_ClrHome();
      return;
    }
    if (len > 0) {
      MLuaCeRun(L, line, len, "=repl");
    }
  }
}
