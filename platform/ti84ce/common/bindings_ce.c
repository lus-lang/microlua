/*
 * MicroLua on the TI-84 Plus CE - calculator bindings.
 *
 * Exposes three globals to Lua, all light C functions:
 *   gfx.*   - graphx drawing (320x240, 8bpp palettized)
 *   key.*   - keypadc polling plus named key constants
 *   timer.* - millisecond clock and sleep
 *
 * gfx.begin() switches the LCD to graphics mode; the frontend calls
 * MLuaCeGfxCleanup() after every chunk so an exiting or erroring script
 * always leaves the OS screen usable.
 */

#include <graphx.h>
#include <keypadc.h>
#include <time.h>

#include "MLuaString.h"
#include "mlua_ce.h"

static Bool GfxActive = FALSE;

static I32 GetIntArg(MLuaState *L, int idx) {
  return (I32)MLuaToNumber(MLuaGetStack(L, idx));
}

/* ---- gfx ---- */

static int GfxBegin(MLuaState *L) {
  (void)L;
  if (!GfxActive) {
    gfx_Begin();
    gfx_SetDrawBuffer();
    GfxActive = TRUE;
  }
  return 0;
}

static int GfxEnd(MLuaState *L) {
  (void)L;
  MLuaCeGfxCleanup();
  return 0;
}

static int GfxSwap(MLuaState *L) {
  (void)L;
  gfx_SwapDraw();
  return 0;
}

static int GfxColor(MLuaState *L) {
  gfx_SetColor((unsigned char)GetIntArg(L, 1));
  return 0;
}

static int GfxFillScreen(MLuaState *L) {
  gfx_FillScreen((unsigned char)GetIntArg(L, 1));
  return 0;
}

static int GfxPixel(MLuaState *L) {
  gfx_SetPixel((unsigned int)GetIntArg(L, 1), (unsigned char)GetIntArg(L, 2));
  return 0;
}

static int GfxLine(MLuaState *L) {
  gfx_Line(GetIntArg(L, 1), GetIntArg(L, 2), GetIntArg(L, 3), GetIntArg(L, 4));
  return 0;
}

static int GfxRect(MLuaState *L) {
  gfx_Rectangle(GetIntArg(L, 1), GetIntArg(L, 2), GetIntArg(L, 3),
                GetIntArg(L, 4));
  return 0;
}

static int GfxFillRect(MLuaState *L) {
  gfx_FillRectangle(GetIntArg(L, 1), GetIntArg(L, 2), GetIntArg(L, 3),
                    GetIntArg(L, 4));
  return 0;
}

static int GfxCircle(MLuaState *L) {
  gfx_Circle(GetIntArg(L, 1), GetIntArg(L, 2), (unsigned int)GetIntArg(L, 3));
  return 0;
}

static int GfxFillCircle(MLuaState *L) {
  gfx_FillCircle(GetIntArg(L, 1), GetIntArg(L, 2),
                 (unsigned int)GetIntArg(L, 3));
  return 0;
}

static int GfxText(MLuaState *L) {
  MLuaValue v = MLuaGetStack(L, 1);
  if (!IsAnyString(v)) {
    L->ErrorMsg = "gfx.text: string expected";
    return -1;
  }
  gfx_PrintStringXY(MLuaStringData(v), GetIntArg(L, 2), GetIntArg(L, 3));
  return 0;
}

static int GfxTextColor(MLuaState *L) {
  gfx_SetTextFGColor((unsigned char)GetIntArg(L, 1));
  if (MLuaGetTop(L) >= 2) {
    gfx_SetTextBGColor((unsigned char)GetIntArg(L, 2));
  }
  return 0;
}

/* ---- key ---- */

/* Scan the keypad controller directly instead of calling the keypadc
 * library's kb_Scan: the installed library on a calculator may be older
 * than the toolchain headers, and the register dance is tiny. Mode 2 is a
 * single scan; the controller returns to idle (0) when the rows are valid. */
#define KB_MODE (*(volatile unsigned char *)0xF50000)

static void KeyScanBlocking(void) {
  KB_MODE = 2;
  while (KB_MODE & 2) {
  }
}

static int KeyScan(MLuaState *L) {
  (void)L;
  KeyScanBlocking();
  return 0;
}

static int KeyIsDown(MLuaState *L) {
  I32 code = GetIntArg(L, 1);
  MLuaPush(L, kb_IsDown((kb_lkey_t)code) ? MLUA_TRUE : MLUA_FALSE);
  return 1;
}

static int KeyAny(MLuaState *L) {
  int g, any = 0;
  for (g = 1; g <= 7; g++) {
    any |= kb_Data[g];
  }
  MLuaPush(L, any ? MLUA_TRUE : MLUA_FALSE);
  return 1;
}

/* Raw row register (1..7) after key.scan(); for debugging custom layouts. */
static int KeyRow(MLuaState *L) {
  I32 g = GetIntArg(L, 1);
  MLuaPush(L, MLuaMakeIntSafe(L, (g >= 1 && g <= 7) ? (I32)kb_Data[g] : 0));
  return 1;
}

/* ---- timer ---- */

static I32 ClockMillis(void) {
  clock_t c = clock();
  /* Split to avoid overflowing 32 bits in c * 1000. */
  return (I32)((c / CLOCKS_PER_SEC) * 1000 +
               (c % CLOCKS_PER_SEC) * 1000 / CLOCKS_PER_SEC);
}

static int TimerMillis(MLuaState *L) {
  MLuaPush(L, MLuaMakeIntSafe(L, ClockMillis()));
  return 1;
}

static int TimerSleep(MLuaState *L) {
  I32 end = ClockMillis() + GetIntArg(L, 1);
  while (ClockMillis() < end) {
    KeyScanBlocking();
    if (kb_IsDown(kb_KeyClear)) {
      break; /* let clear interrupt sleeping loops */
    }
  }
  return 0;
}

/* ---- registration ---- */

static const MLuaLibEntry GfxEntries[] = {
    /* "finish", not "end": end is a Lua keyword, gfx.end would not parse */
    {"begin", GfxBegin},   {"finish", GfxEnd},
    {"swap", GfxSwap},     {"color", GfxColor},
    {"fill", GfxFillScreen}, {"pixel", GfxPixel},
    {"line", GfxLine},     {"rect", GfxRect},
    {"fillRect", GfxFillRect}, {"circle", GfxCircle},
    {"fillCircle", GfxFillCircle}, {"text", GfxText},
    {"textColor", GfxTextColor}, {NULL, NULL}};

static const MLuaLibEntry KeyEntries[] = {{"scan", KeyScan},
                                          {"isDown", KeyIsDown},
                                          {"any", KeyAny},
                                          {"row", KeyRow},
                                          {NULL, NULL}};

static const MLuaLibEntry TimerEntries[] = {{"millis", TimerMillis},
                                            {"sleep", TimerSleep},
                                            {NULL, NULL}};

static void SetConst(MLuaState *L, MLuaValue lib, const char *name, I32 v) {
  Size n = 0;
  while (name[n]) {
    n++;
  }
  MLuaTableSet(L, lib, MLuaStringNew(L, name, n), MLuaMakeIntSafe(L, v));
}

void MLuaCeOpenLibs(MLuaState *L) {
  MLuaValue gfx = MLuaNewLib(L, "gfx");
  MLuaValue key = MLuaNewLib(L, "key");
  MLuaValue timer = MLuaNewLib(L, "timer");
  MLuaRegisterLib(L, gfx, GfxEntries);
  MLuaRegisterLib(L, key, KeyEntries);
  MLuaRegisterLib(L, timer, TimerEntries);

  SetConst(L, key, "up", (I32)kb_KeyUp);
  SetConst(L, key, "down", (I32)kb_KeyDown);
  SetConst(L, key, "left", (I32)kb_KeyLeft);
  SetConst(L, key, "right", (I32)kb_KeyRight);
  SetConst(L, key, "enter", (I32)kb_KeyEnter);
  SetConst(L, key, "clear", (I32)kb_KeyClear);
  SetConst(L, key, "second", (I32)kb_Key2nd);
  SetConst(L, key, "alpha", (I32)kb_KeyAlpha);
  SetConst(L, key, "mode", (I32)kb_KeyMode);
  SetConst(L, key, "del", (I32)kb_KeyDel);
}

void MLuaCeGfxCleanup(void) {
  if (GfxActive) {
    gfx_End();
    GfxActive = FALSE;
  }
}
