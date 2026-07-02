/*
 * MicroLua on the TI-84 Plus CE - appvar script access.
 *
 * Scripts live in appvars (usually archived): either MicroLua bytecode
 * produced by `mlua -o` (sniffed by its \x1BMLu magic) or, on compiler
 * builds, plain Lua source. Variables are accessed through the OS symbol
 * table (ROM calls - no library dependency); data is executed directly
 * from the archive, which is safe read-only because the frontend performs
 * no VAT-shifting operations while a chunk runs.
 */

#include <ti/vars.h>

#include "mlua_ce.h"

static Bool IsBytecode(const unsigned char *data, unsigned int size) {
  return size >= 4 && data[0] == 0x1B && data[1] == 'M' && data[2] == 'L' &&
         data[3] == 'u';
}

#if MLUA_ENABLE_COMPILER
/* Accept anything that plausibly starts as Lua source text. Binary appvars
 * (app data, shell configs) often begin with a printable byte, so scan a
 * prefix rather than trusting the first byte alone. */
static Bool IsSourceText(const unsigned char *data, unsigned int size) {
  unsigned int i, n = size < 32 ? size : 32;
  if (n == 0) {
    return FALSE;
  }
  for (i = 0; i < n; i++) {
    unsigned char c = data[i];
    if (c != '\t' && c != '\n' && c != '\r' && (c < ' ' || c >= 0x7F)) {
      return FALSE;
    }
  }
  return TRUE;
}
#endif

const char *MLuaCeLoadVar(const char *name, Size *lenOut) {
  var_t *v = os_GetAppVarData(name, NULL);
  if (!v) {
    return 0;
  }
  *lenOut = v->size;
  return (const char *)v->data;
}

static Bool IsRunnable(const char *name) {
  Size len;
  const unsigned char *data = (const unsigned char *)MLuaCeLoadVar(name, &len);
  if (!data) {
    return FALSE;
  }
  if (IsBytecode(data, len)) {
    return TRUE;
  }
#if MLUA_ENABLE_COMPILER
  return IsSourceText(data, len);
#else
  return FALSE;
#endif
}

Size MLuaCeListScripts(char names[][9], Size max) {
  Size count = 0;
  void *entry = os_GetSymTablePtr();
  unsigned int type, nameLen;
  char name[9];
  void *data;

  while (count < max &&
         (entry = os_NextSymEntry(entry, &type, &nameLen, name, &data))) {
    Size n = nameLen > 8 ? 8 : nameLen;
    name[n] = '\0';
    if (type != OS_TYPE_APPVAR || name[0] == '!' || name[0] == '#' ||
        (unsigned char)name[0] < ' ') {
      continue; /* skip system/hidden appvars */
    }
    if (!IsRunnable(name)) {
      continue;
    }
    for (n = 0; name[n] && n < 8; n++) {
      names[count][n] = name[n];
    }
    names[count][n] = '\0';
    count++;
  }
  return count;
}

MLuaValue MLuaCeRequire(MLuaState *L, const char *modname) {
  char name[9];
  const char *data;
  Size len, i;
  Size savedEvalTop;
  MLuaStatus status;

  /* Module names map to appvar names: uppercased, truncated to 8 chars. */
  for (i = 0; modname[i] && i < 8; i++) {
    char c = modname[i];
    name[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
  }
  name[i] = '\0';

  data = MLuaCeLoadVar(name, &len);
  if (!data) {
    return MLUA_NIL;
  }

  /* Mirror the reference requirer (MLuaRepl.c): run the module chunk, then
   * pop its result so leftovers don't leak into the caller's frame. */
  savedEvalTop = L->EvalTop;
  status = MLuaDoBuffer(L, data, len, modname);
  if (status != MLUA_OK) {
    return MLUA_NIL;
  }
  if (L->EvalTop > savedEvalTop) {
    MLuaValue result = L->EvalStack[L->EvalTop - 1];
    L->EvalTop = savedEvalTop;
    return result;
  }
  return MLUA_TRUE;
}
