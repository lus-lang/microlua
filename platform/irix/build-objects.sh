#!/bin/bash
# Cross-compile MicroLua objects for SGI IRIX 6.5 (mips3/n32, big-endian).
#
# GNU ld cannot produce IRIX-runtime-compatible binaries (rld segfaults on
# its dynamic layout and it mis-resolves MIPSpro composite relocs), so the
# final link happens ON the IRIX box with the native SGI ld — see README.md
# and link-on-box.csh. Runs inside the irix-cross Docker image (built from
# Dockerfile.toolchain + build-toolchain.sh) with the repo mounted at
# /mnt/microlua:
#
#   docker run --rm -v "$PWD":/mnt/microlua \
#       -v "$PWD/platform/irix/build-objects.sh":/build-objects.sh:ro \
#       irix-cross bash /build-objects.sh
set -ex

CROSS=/opt/cross/bin/mips-sgi-irix6.5-gcc
OBJCOPY=/opt/cross/bin/mips-sgi-irix6.5-objcopy
SRC=/mnt/microlua
OUT=$SRC/builddir-irix
PORT='-DMLUA_PORT_HEADER="ports/irix_mips.h"'

# Section hygiene for the native SGI linker: no .eh_frame, no .debug_frame,
# no SHF_MERGE string sections, no .text.startup, no .comment. Without these
# ld32 dies with "Internal: at ../../ld/write.c writing beyond end-of-file".
IRIX_LD_COMPAT="-fno-asynchronous-unwind-tables -fno-unwind-tables \
  -fno-merge-constants -fno-reorder-functions -fno-ident -fno-dwarf2-cfi-asm"

LIB_CFLAGS="-O2 -std=c99 -Wall $IRIX_LD_COMPAT -ffreestanding -fno-builtin \
  -fno-stack-protector -DMLUA_ENABLE_COMPILER=1 $PORT -I$SRC/src"
REPL_CFLAGS="-O2 -std=c99 -Wall $IRIX_LD_COMPAT -DMLUA_ENABLE_COMPILER=1 $PORT -I$SRC/src"

LIB_SOURCES="MLuaCore MLuaValue MLuaAlloc MLuaGC MLuaString MLuaTable MLuaUTF8 \
  MLuaCode MLuaDump MLuaUndump MLuaFunc MLuaThread MLuaVM MLuaConvert \
  MLuaLex MLuaParse"
LIB_SOURCES_LIBRARY="MLuaMathLib MLuaCoroLib MLuaStringLib MLuaTableLib MLuaBaseLib"

rm -rf $OUT
mkdir -p $OUT/obj

for f in $LIB_SOURCES; do
  $CROSS $LIB_CFLAGS -c -o $OUT/obj/$f.o $SRC/src/$f.c
done
for f in $LIB_SOURCES_LIBRARY; do
  $CROSS $LIB_CFLAGS -c -o $OUT/obj/$f.o $SRC/src/library/$f.c
done
$CROSS $REPL_CFLAGS -c -o $OUT/obj/MLuaRepl.o $SRC/src/MLuaRepl.c
$CROSS $REPL_CFLAGS -c -o $OUT/obj/MLuaStdLib.o $SRC/src/extensions/MLuaStdLib.c

for o in $OUT/obj/*.o; do
  $OBJCOPY --remove-section=.debug_frame --remove-section=.comment \
           --remove-section=.pdr --remove-section=.mdebug $o
done

# Anything beyond libm/libc here means the box link will need more inputs.
/opt/cross/bin/mips-sgi-irix6.5-nm -u $OUT/obj/*.o | grep '__' | sort -u || true

tar -cf $OUT/mlua-objs.tar -C $OUT/obj .
echo MLUA-IRIX-OBJECTS-OK
