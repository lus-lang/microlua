/*
 * MicroLua on the TI-84 Plus CE - OS home-screen console.
 *
 * The home screen is 26x10 characters; os_PutStrFull wraps at the right
 * edge and os_NewLine scrolls at the bottom, so the OS does the layout.
 */

#include <ti/getcsc.h>
#include <ti/screen.h>

#include "mlua_ce.h"

void MLuaCeConsoleInit(void) { os_ClrHome(); }

void MLuaCeConsoleWrite(const char *msg, Size len) {
  char seg[32];
  Size i = 0;
  while (i < len) {
    Size n = 0;
    if (msg[i] == '\n') {
      os_NewLine();
      i++;
      continue;
    }
    while (i < len && msg[i] != '\n' && n < sizeof(seg) - 1) {
      char c = msg[i++];
      seg[n++] = (c == '\t') ? ' ' : c;
    }
    seg[n] = '\0';
    os_PutStrFull(seg);
  }
}

void MLuaCeConsoleWriteStr(const char *msg) {
  Size len = 0;
  while (msg[len]) {
    len++;
  }
  MLuaCeConsoleWrite(msg, len);
}

void MLuaCeConsolePause(void) {
  while (os_GetCSC()) {
  }
  while (!os_GetCSC()) {
  }
}
