# ----------------------------
# MicroLua (full build) for the TI-84 Plus CE (CE C Toolchain).
# Includes the Lua compiler: runs source or bytecode appvars and
# provides an on-calc REPL (mode key in the picker).
# ----------------------------

NAME = MLUA
DESCRIPTION = "MicroLua"
# Compression keeps this a single .8xp (zx0 packs ~7% denser than zx7,
# which is what gets the full build under the 64 KB program cap); archived
# because the stored program and its decompressed image cannot both fit in
# user RAM.
COMPRESSED = YES
COMPRESSED_MODE = zx0
ARCHIVED = YES

HAS_PRINTF = NO
LTO = YES
MLUA_SRC = ../../../src

CFLAGS = -Wall -Wextra -Oz \
	-DMLUA_PORT_HEADER=\"ports/ti84ce.h\" \
	-DMLUA_ENABLE_COMPILER=1 \
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
	$(MLUA_SRC)/MLuaLex.c \
	$(MLUA_SRC)/MLuaParse.c \
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
