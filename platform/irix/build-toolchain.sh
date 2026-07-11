#!/bin/bash
# Build a mips-sgi-irix6.5 cross-toolchain (binutils 2.24 + gcc 4.7.4, C only).
# Runs inside the debian:jessie amd64 container. Sources mounted ro at /src,
# IRIX sysroot mounted ro at /mnt/sysroot. Installs to /opt/cross with the
# sysroot copied to /opt/irix/sysroot so the committed image is self-contained.
set -ex

TARGET=mips-sgi-irix6.5
PREFIX=/opt/cross
SYSROOT=/opt/irix/sysroot
JOBS=$(nproc)

mkdir -p /opt/irix
cp -a /mnt/sysroot $SYSROOT

mkdir -p /build
cd /build

# --- binutils 2.24 ---
tar xjf /src/binutils-2.24.tar.bz2
mkdir -p b-binutils && cd b-binutils
../binutils-2.24/configure --target=$TARGET --prefix=$PREFIX \
  --with-sysroot=$SYSROOT --disable-werror --disable-nls
make -j$JOBS MAKEINFO=true
make install MAKEINFO=true
cd /build

export PATH=$PREFIX/bin:$PATH

# --- gcc 4.7.4 (C only, in-tree gmp/mpfr/mpc) ---
tar xjf /src/gcc-4.7.4.tar.bz2
cd gcc-4.7.4
tar xjf /src/gmp-4.3.2.tar.bz2  && mv gmp-4.3.2 gmp
tar xjf /src/mpfr-2.4.2.tar.bz2 && mv mpfr-2.4.2 mpfr
tar xzf /src/mpc-0.8.1.tar.gz   && mv mpc-0.8.1 mpc
cd /build
mkdir -p b-gcc && cd b-gcc
../gcc-4.7.4/configure --target=$TARGET --prefix=$PREFIX \
  --with-sysroot=$SYSROOT --enable-obsolete \
  --enable-languages=c --disable-shared --disable-nls --disable-multilib \
  --disable-libssp --disable-libgomp --disable-libmudflap --disable-libquadmath \
  --disable-libitm --with-gnu-as --with-gnu-ld \
  --with-abi=n32 --with-arch=mips3 MAKEINFO=missing
make -j$JOBS
make install

# --- smoke test: compile and inspect a trivial program ---
cat > /tmp/hello.c <<'EOF'
#include <stdio.h>
int main(void) { printf("hello from IRIX cross\n"); return 0; }
EOF
$PREFIX/bin/$TARGET-gcc -o /tmp/hello /tmp/hello.c
file /tmp/hello
$PREFIX/bin/$TARGET-gcc -O2 -c -o /tmp/hello.o /tmp/hello.c
file /tmp/hello.o

# keep the image lean before commit
rm -rf /build
echo TOOLCHAIN-BUILD-OK
