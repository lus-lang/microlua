/*
 * MicroLua (MLUA) for the TI-84 Plus CE: script runner + interactive REPL.
 *
 * Runs Lua source or bytecode from appvars (picker), and mode opens the
 * REPL. Source appvars are compiled on the calculator.
 */

#include "mlua_ce.h"

int main(void) {
  MLuaCeRunLoop();
  return 0;
}
