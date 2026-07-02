/*
 * MicroLua on the TI-84 Plus CE - state lifecycle, chunk execution, and
 * the script picker.
 */

#include <ti/getcsc.h>
#include <ti/screen.h>

#include "MLuaConvert.h"
#include "mlua_ce.h"

static char LuaHeap[MLUA_CE_HEAP_SIZE] MLUA_ALIGNAS(8);

static void CeOutput(MLuaState *L, int kind, const char *msg, Size len) {
  (void)L;
  (void)kind; /* the core only emits PRINT; errors surface via ErrorMsg */
  MLuaCeConsoleWrite(msg, len);
}

MLuaState *MLuaCeNewState(void) {
  MLuaState *L = MLuaNewConstrainedState(LuaHeap, sizeof(LuaHeap));
  if (!L) {
    return NULL;
  }
  MLuaSetOutput(L, CeOutput);
  MLuaSetRequirer(L, MLuaCeRequire);
  MLuaOpenLibs(L);
  MLuaCeOpenLibs(L);
  return L;
}

MLuaStatus MLuaCeRun(MLuaState *L, const char *data, Size len,
                     const char *name) {
  MLuaStatus status = MLuaDoBuffer(L, data, len, name);
  if (status != MLUA_OK) {
    MLuaCeConsoleWriteStr("E: ");
    if (L->ErrorMsg) {
      if (L->ErrorLine > 0) {
        char num[16];
        Size n = MLuaIntToStr((I64)L->ErrorLine, num);
        MLuaCeConsoleWriteStr("line ");
        MLuaCeConsoleWrite(num, n);
        MLuaCeConsoleWriteStr(": ");
      }
      MLuaCeConsoleWriteStr(L->ErrorMsg);
    } else {
      MLuaCeConsoleWriteStr("unknown error");
    }
    MLuaCeConsoleWrite("\n", 1);
  }
  return status;
}

/* ---- script picker (OS home screen, 26x10) ---- */

#define PICKER_ROWS 8 /* rows 1..8 show entries; row 0 title, row 9 keys */

static void PickerDraw(char names[][9], Size count, Size sel, Size top) {
  Size row;
  os_ClrHome();
  os_PutStrFull(count ? "MicroLua - choose script" : "MicroLua - no scripts");
  for (row = 0; row < PICKER_ROWS && top + row < count; row++) {
    os_SetCursorPos((unsigned char)(row + 1), 0);
    os_PutStrFull(top + row == sel ? "> " : "  ");
    os_PutStrFull(names[top + row]);
  }
  os_SetCursorPos(9, 0);
#if MLUA_ENABLE_COMPILER
  os_PutStrFull(count ? "enter:run mode:repl clear:x" : "mode:repl clear:exit");
#else
  os_PutStrFull(count ? "enter:run  clear:exit" : "clear:exit");
#endif
}

/* Run one script in a fresh state (the heap is re-initialized per run so
 * scripts never see each other's globals or garbage). */
static void RunByName(const char *name) {
  Size len;
  const char *data = MLuaCeLoadVar(name, &len);
  MLuaState *L;

  MLuaCeConsoleInit();
  if (!data) {
    MLuaCeConsoleWriteStr("E: cannot open appvar\n");
    MLuaCeConsolePause();
    return;
  }
  L = MLuaCeNewState();
  if (!L) {
    MLuaCeConsoleWriteStr("E: state creation failed\n");
    MLuaCeConsolePause();
    return;
  }
  MLuaCeRun(L, data, len, name);
  MLuaCeGfxCleanup(); /* restore the OS screen if the script used gfx */
  /* No '[' or ']': the OS large font renders those codepoints as theta. */
  MLuaCeConsoleWriteStr("-- done: press a key --");
  MLuaCeConsolePause();
}

void MLuaCeRunLoop(void) {
  static char names[MLUA_CE_MAX_SCRIPTS][9];
  for (;;) {
    Size count = MLuaCeListScripts(names, MLUA_CE_MAX_SCRIPTS);
    Size sel = 0, top = 0;
    Bool picking = TRUE;
    while (picking) {
      unsigned char key;
      PickerDraw(names, count, sel, top);
      do {
        key = os_GetCSC();
      } while (!key);
      switch (key) {
      case sk_Up:
        if (sel > 0) {
          sel--;
          if (sel < top) {
            top = sel;
          }
        }
        break;
      case sk_Down:
        if (count && sel + 1 < count) {
          sel++;
          if (sel >= top + PICKER_ROWS) {
            top = sel - (PICKER_ROWS - 1);
          }
        }
        break;
      case sk_Enter:
        if (count) {
          RunByName(names[sel]);
          picking = FALSE; /* re-enumerate: the script may have written vars */
        }
        break;
      case sk_Clear:
        os_ClrHome();
        return;
#if MLUA_ENABLE_COMPILER
      case sk_Mode:
        MLuaCeRepl();
        picking = FALSE; /* re-enumerate after the REPL session */
        break;
#endif
      default:
        break;
      }
    }
  }
}
