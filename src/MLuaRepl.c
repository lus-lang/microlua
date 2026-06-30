/*
 * MicroLua - MLuaRepl.c
 * Command-line REPL and script executor
 *
 * Usage: mlua [options] [file [args]]
 *   -h, --help            List options
 *   -e, --eval EXPR       Evaluate EXPR
 *   -i, --interactive     Go to interactive mode
 *   -I, --include FILE    Include (require) FILE
 *   -d, --dump            Dump memory usage stats
 *       --memory-limit N  Limit memory to N bytes
 *       --no-column       No column in debug info
 *   -o FILE               Save bytecode to FILE
 *   -v, --version         Print version
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "MLuaAlloc.h"
#include "MLuaCore.h"
#include "MLuaDump.h"
#include "MLuaStdLib.h"
#include "MLuaString.h"
#include "MLuaVM.h"

#define MLUA_VERSION "MicroLua 1.0.0"
#define MLUA_COPYRIGHT "Copyright (C) 2024"

/* Heap buffer for constrained mode */
static U8 HeapBuffer[16 * 1024 * 1024]; /* 16 MB default */

/* Current state for callbacks */
static MLuaState *CurrentState = NULL;

/* ========================================================================== */
/* Output Callback                                                            */
/* ========================================================================== */

static void ReplOutput(MLuaState *L, int kind, const char *msg, Size len) {
  (void)L;
  (void)kind;
  fwrite(msg, 1, len, kind == MLUA_OUTPUT_ERROR ? stderr : stdout);
  fflush(kind == MLUA_OUTPUT_ERROR ? stderr : stdout);
}

/* ========================================================================== */
/* Require Callback                                                           */
/* ========================================================================== */

static char *ReadFile(const char *path, Size *outLen) {
  FILE *f = fopen(path, "rb");
  char *buf;
  long size;

  if (!f)
    return NULL;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  size = ftell(f);
  if (size < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }

  buf = (char *)malloc((size_t)size + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }

  fread(buf, 1, (size_t)size, f);
  buf[size] = '\0';
  fclose(f);

  if (outLen)
    *outLen = (Size)size;
  return buf;
}

static MLuaValue ReplRequire(MLuaState *L, const char *modname) {
  char path[512];
  char *source;
  Size len;
  MLuaStatus status;
  Size savedEvalTop;

  /* Simple module resolution: look for modname.lua */
  snprintf(path, sizeof(path), "%s.lua", modname);

  source = ReadFile(path, &len);
  if (!source) {
    return MLUA_NIL;
  }

  /* Save EvalTop before executing module.
   * Three-array architecture: results are on EvalStack.
   */
  savedEvalTop = L->EvalTop;
  status = MLuaDoString(L, source, len, modname);
  free(source);

  if (status != MLUA_OK) {
    return MLUA_NIL;
  }

  /* Check if module returned a value.
   * If EvalTop > savedEvalTop, the top value is the result. Reset the
   * stack to where it was so the module chunk's leftovers don't leak
   * into the caller's frame (the caller re-pushes the result).
   */
  if (L->EvalTop > savedEvalTop) {
    MLuaValue result = L->EvalStack[L->EvalTop - 1];
    L->EvalTop = savedEvalTop;
    return result;
  }

  /* No return value - return true to indicate successful load */
  return MLUA_TRUE;
}

/* ========================================================================== */
/* Help and Version                                                           */
/* ========================================================================== */

static void PrintHelp(const char *progname) {
  printf("Usage: %s [options] [file [args]]\n", progname);
  printf("Options:\n");
  printf("  -h, --help            List options\n");
  printf("  -e, --eval EXPR       Evaluate EXPR\n");
  printf("  -i, --interactive     Go to interactive mode\n");
  printf("  -I, --include FILE    Include (require) FILE\n");
  printf("  -d, --dump            Dump memory usage stats\n");
  printf("      --memory-limit N  Limit memory to N bytes\n");
  printf("      --no-column       No column in debug info\n");
  printf("  -o FILE               Save bytecode to FILE\n");
  printf("  -v, --version         Print version\n");
}

static void PrintVersion(void) {
  printf("%s\n", MLUA_VERSION);
  printf("%s\n", MLUA_COPYRIGHT);
}

static int WriteBytecode(MLuaState *L, const char *source, Size len,
                         const char *name, const char *outputFile) {
  MLuaStatus status;
  MLuaValue func;
  Size size;
  char *buf;
  FILE *out;
  size_t written;
  int closeStatus;

  status = MLuaLoadString(L, source, len, name);
  if (status != MLUA_OK) {
    if (L->ErrorMsg) {
      fprintf(stderr, "%s: Error: %s\n", name, L->ErrorMsg);
    } else {
      fprintf(stderr, "%s: Error: compile error\n", name);
    }
    return 1;
  }

  func = MLuaPop(L);
  size = MLuaDumpFunction(L, func, NULL, 0);
  if (size == 0) {
    fprintf(stderr, "Error: cannot dump bytecode\n");
    return 1;
  }

  buf = (char *)malloc((size_t)size);
  if (!buf) {
    fprintf(stderr, "Error: out of memory\n");
    return 1;
  }
  MLuaDumpFunction(L, func, buf, size);

  out = fopen(outputFile, "wb");
  if (!out) {
    fprintf(stderr, "Error: Cannot open '%s' for writing\n", outputFile);
    free(buf);
    return 1;
  }

  written = fwrite(buf, 1, (size_t)size, out);
  closeStatus = fclose(out);
  if (written != (size_t)size || closeStatus != 0) {
    fprintf(stderr, "Error: failed to write '%s'\n", outputFile);
    free(buf);
    return 1;
  }

  free(buf);
  return 0;
}

/* ========================================================================== */
/* Interactive REPL                                                           */
/* ========================================================================== */

static void RunInteractive(MLuaState *L) {
  char line[4096];

  printf("%s\n", MLUA_VERSION);
  printf("Type Lua code or 'quit' to exit.\n\n");

  while (1) {
    printf("> ");
    fflush(stdout);

    if (!fgets(line, sizeof(line), stdin)) {
      printf("\n");
      break;
    }

    /* Remove trailing newline */
    Size len = StrLen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }

    /* Check for quit */
    if (StrCmp(line, "quit") == 0 || StrCmp(line, "exit") == 0) {
      break;
    }

    /* Skip empty lines */
    if (len == 0)
      continue;

    /* Execute line */
    MLuaStatus status = MLuaDoString(L, line, len, "=stdin");
    if (status != MLUA_OK) {
      if (L->ErrorMsg) {
        if (L->ErrorLine > 0) {
          fprintf(stderr, "[line %llu]: Error: %s\n",
                  (unsigned long long)L->ErrorLine, L->ErrorMsg);
        } else {
          fprintf(stderr, "Error: %s\n", L->ErrorMsg);
        }
        /* Print stacktrace if available */
        if (L->StackTrace && L->StackTrace[0] != '\0') {
          fprintf(stderr, "stack traceback:\n%s", L->StackTrace);
        }
      } else {
        fprintf(stderr, "Error: (unknown)\n");
      }
    }
  }
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(int argc, char **argv) {
  MLuaState *L;
  int i;
  int interactive = 0;
  int dumpMemory = 0;
  Size memoryLimit = sizeof(HeapBuffer);
  const char *scriptFile = NULL;
  const char *outputFile = NULL;
  const char *evalExpr = NULL;

  /* Parse arguments */
  for (i = 1; i < argc; i++) {
    const char *arg = argv[i];

    if (arg[0] == '-') {
      if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
        PrintHelp(argv[0]);
        return 0;
      } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
        PrintVersion();
        return 0;
      } else if (strcmp(arg, "-i") == 0 || strcmp(arg, "--interactive") == 0) {
        interactive = 1;
      } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--dump") == 0) {
        dumpMemory = 1;
      } else if (strcmp(arg, "-e") == 0 || strcmp(arg, "--eval") == 0) {
        if (i + 1 < argc) {
          evalExpr = argv[++i];
        } else {
          fprintf(stderr, "Error: -e requires an argument\n");
          return 1;
        }
      } else if (strcmp(arg, "-I") == 0 || strcmp(arg, "--include") == 0) {
        if (i + 1 < argc) {
          /* Handle includes after state creation */
          i++;
        } else {
          fprintf(stderr, "Error: -I requires an argument\n");
          return 1;
        }
      } else if (strcmp(arg, "-o") == 0) {
        if (i + 1 < argc) {
          outputFile = argv[++i];
        } else {
          fprintf(stderr, "Error: -o requires an argument\n");
          return 1;
        }
      } else if (strcmp(arg, "--memory-limit") == 0) {
        if (i + 1 < argc) {
          memoryLimit = (Size)atol(argv[++i]);
          if (memoryLimit > sizeof(HeapBuffer)) {
            memoryLimit = sizeof(HeapBuffer);
          }
        } else {
          fprintf(stderr, "Error: --memory-limit requires an argument\n");
          return 1;
        }
      } else if (strcmp(arg, "--no-column") == 0) {
        /* Ignored for now */
      } else {
        fprintf(stderr, "Unknown option: %s\n", arg);
        return 1;
      }
    } else {
      /* First non-option is the script file; the remaining args reach the
       * script through the standard `arg` table built below */
      if (!scriptFile) {
        scriptFile = arg;
      }
    }
  }

  if (outputFile && !scriptFile && !evalExpr) {
    fprintf(stderr, "Error: -o requires a script file or -e expression\n");
    return 1;
  }

  /* Default to interactive if no script or eval */
  if (!scriptFile && !evalExpr && !outputFile) {
    interactive = 1;
  }

  /* Create state */
  L = MLuaNewConstrainedState(HeapBuffer, memoryLimit);
  if (!L) {
    fprintf(stderr, "Error: Failed to create Lua state\n");
    return 1;
  }
  CurrentState = L;

  /* Set up I/O */
  MLuaSetOutput(L, ReplOutput);
  MLuaSetRequirer(L, ReplRequire);

  /* Open standard libraries + the optional io/os extension (footnote 2 of
   * the README: the repl provides these) */
  MLuaOpenLibs(L);
  MLuaOpenStdLib(L);

  /* Set up arg table */
  {
    MLuaValue argTable = MLuaTableNewSized(L, (Size)argc, 0);
    int argIdx = 0;
    int scriptIdx = -1;
    int j;

    /* Find script file position for arg[0] */
    for (j = 1; j < argc; j++) {
      if (argv[j][0] != '-' && scriptIdx < 0) {
        scriptIdx = j;
        break;
      }
      /* Skip option arguments */
      if ((strcmp(argv[j], "-e") == 0 || strcmp(argv[j], "-I") == 0 ||
           strcmp(argv[j], "-o") == 0 ||
           strcmp(argv[j], "--memory-limit") == 0) &&
          j + 1 < argc) {
        j++;
      }
    }

    /* arg[0] = script name (or interpreter if no script) */
    if (scriptIdx >= 0) {
      MLuaTableSet(L, argTable, MakeInt(0),
                   MLuaStringNew(L, argv[scriptIdx], StrLen(argv[scriptIdx])));
      /* Script arguments at positive indices */
      for (j = scriptIdx + 1; j < argc; j++) {
        argIdx++;
        MLuaTableSet(L, argTable, MakeInt(argIdx),
                     MLuaStringNew(L, argv[j], StrLen(argv[j])));
      }
      /* Interpreter name at arg[-1] */
      MLuaTableSet(L, argTable, MakeInt(-1),
                   MLuaStringNew(L, argv[0], StrLen(argv[0])));
    } else {
      /* No script: arg[0] = interpreter name */
      MLuaTableSet(L, argTable, MakeInt(0),
                   MLuaStringNew(L, argv[0], StrLen(argv[0])));
    }

    MLuaSetGlobal(L, "arg", argTable);
  }

  /* Handle -I includes (second pass) */
  for (i = 1; i < argc; i++) {
    if ((strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--include") == 0) &&
        i + 1 < argc) {
      char *source;
      Size len;
      const char *path = argv[i + 1];

      source = ReadFile(path, &len);
      if (source) {
        MLuaDoString(L, source, len, path);
        free(source);
      } else {
        fprintf(stderr, "Warning: Could not read '%s'\n", path);
      }
      i++;
    }
  }

  if (outputFile) {
    if (evalExpr) {
      if (WriteBytecode(L, evalExpr, StrLen(evalExpr), "=eval", outputFile) !=
          0) {
        return 1;
      }
    } else if (scriptFile) {
      char *source;
      Size len;

      source = ReadFile(scriptFile, &len);
      if (!source) {
        fprintf(stderr, "Error: Cannot open '%s'\n", scriptFile);
        return 1;
      }
      if (WriteBytecode(L, source, len, scriptFile, outputFile) != 0) {
        free(source);
        return 1;
      }
      free(source);
    }
  } else if (evalExpr) {
    MLuaStatus status = MLuaDoString(L, evalExpr, StrLen(evalExpr), "=eval");
    if (status != MLUA_OK) {
      if (L->ErrorMsg) {
        if (L->ErrorLine > 0) {
          fprintf(stderr, "[line %llu]: Error: %s\n",
                  (unsigned long long)L->ErrorLine, L->ErrorMsg);
        } else {
          fprintf(stderr, "Error: %s\n", L->ErrorMsg);
        }
        /* Print stacktrace if available */
        if (L->StackTrace && L->StackTrace[0] != '\0') {
          fprintf(stderr, "stack traceback:\n%s", L->StackTrace);
        }
      }
      return 1;
    }
  }

  /* Execute script file */
  if (!outputFile && scriptFile) {
    char *source;
    Size len;

    source = ReadFile(scriptFile, &len);
    if (!source) {
      fprintf(stderr, "Error: Cannot open '%s'\n", scriptFile);
      return 1;
    }

    MLuaStatus status = MLuaDoString(L, source, len, scriptFile);
    free(source);

    if (status != MLUA_OK) {
      if (L->ErrorMsg) {
        if (L->ErrorLine > 0) {
          fprintf(stderr, "%s:%llu: Error: %s\n", scriptFile,
                  (unsigned long long)L->ErrorLine, L->ErrorMsg);
        } else {
          fprintf(stderr, "%s: Error: %s\n", scriptFile, L->ErrorMsg);
        }
        /* Print stacktrace if available */
        if (L->StackTrace && L->StackTrace[0] != '\0') {
          fprintf(stderr, "stack traceback:\n%s", L->StackTrace);
        }
      }
      return 1;
    }
  }

  /* Run interactive mode */
  if (interactive) {
    RunInteractive(L);
  }

  /* Dump memory stats */
  if (dumpMemory) {
    printf("\n=== Memory Stats ===\n");
    printf("Heap Size:    %llu bytes\n", (unsigned long long)L->HeapSize);
    printf("Heap Used:    %llu bytes\n", (unsigned long long)L->HeapTop);
    printf("Heap Free:    %llu bytes\n",
           (unsigned long long)(L->HeapSize - L->HeapTop));
    printf("EvalStack:    %llu (top=%llu)\n",
           (unsigned long long)L->EvalStackSize,
           (unsigned long long)L->EvalTop);
    printf("Locals:       %llu (base=%llu)\n",
           (unsigned long long)L->LocalsSize,
           (unsigned long long)L->LocalsBase);
    printf("Args:         %llu (count=%llu)\n", (unsigned long long)L->ArgsSize,
           (unsigned long long)L->ArgsCount);
    printf("Strings:      %llu\n", (unsigned long long)L->StringTableCount);
    printf("Light Funcs:  %llu\n", (unsigned long long)L->LightFuncCount);
  }

  return 0;
}
