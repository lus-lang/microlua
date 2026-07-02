/*
 * MicroLua bytecode runner (MLUAR) for the TI-84 Plus CE.
 *
 * Lists bytecode appvars (compile on a PC with `mlua -o script.mlu` and
 * transfer with convbin) and runs the selected one.
 */

#include "mlua_ce.h"

int main(void) {
  MLuaCeRunLoop();
  return 0;
}
