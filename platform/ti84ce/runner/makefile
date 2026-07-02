# ----------------------------
# MicroLua bytecode runner for the TI-84 Plus CE (CE C Toolchain).
# Bytecode-only: the Lua compiler (MLuaLex/MLuaParse) is excluded; run
# .mlu chunks produced on a PC with `mlua -o`.
# ----------------------------

NAME = MLUAR
DESCRIPTION = "MicroLua bytecode runner"
# Compression is what keeps this a single .8xp: the ~106 KB image packs to
# ~48 KB with zx0, under the 64 KB program cap (uncompressed it would
# auto-split into a launcher + code appvars).
COMPRESSED = YES
COMPRESSED_MODE = zx0
# Archived: the stored program (~52 KB) must live in flash, or it and its
# ~106 KB decompressed image cannot both fit in user RAM.
ARCHIVED = YES

HAS_PRINTF = NO
LTO = YES

MLUA_SRC = ../../../src

# Typed float arrays (~2.8 KB of image) fit here but not in the repl build;
# see the note in ports/ti84ce.h.
CFLAGS = -Wall -Wextra -Oz \
	-DMLUA_PORT_HEADER=\"ports/ti84ce.h\" \
	-DMLUA_ENABLE_COMPILER=0 \
	-DMLUA_TABLE_NUM_ARRAYS=1 \
	-I$(MLUA_SRC) -I$(MLUA_SRC)/library -I../common
CXXFLAGS = $(CFLAGS)

EXTRA_C_SOURCES = \
	$(MLUA_SRC)/MLuaCore.c \
	$(MLUA_SRC)/MLuaValue.c \
	$(MLUA_SRC)/MLuaAlloc.c \
	$(MLUA_SRC)/MLuaGC.c \
	$(MLUA_SRC)/MLuaString.c \
	$(MLUA_SRC)/MLuaTable.c \
	$(MLUA_SRC)/MLuaUTF8.c \
	$(MLUA_SRC)/MLuaCode.c \
	$(MLUA_SRC)/MLuaUndump.c \
	$(MLUA_SRC)/MLuaFunc.c \
	$(MLUA_SRC)/MLuaThread.c \
	$(MLUA_SRC)/MLuaVM.c \
	$(MLUA_SRC)/MLuaConvert.c \
	$(MLUA_SRC)/library/MLuaMathLib.c \
	$(MLUA_SRC)/library/MLuaCoroLib.c \
	$(MLUA_SRC)/library/MLuaStringLib.c \
	$(MLUA_SRC)/library/MLuaTableLib.c \
	$(MLUA_SRC)/library/MLuaBaseLib.c \
	../common/bindings_ce.c \
	../common/console.c \
	../common/requirer.c \
	../common/runner.c

# ----------------------------

include $(shell cedev-config --makefile)
