#!/bin/csh
cd $HOME
rm -rf objs mlua
mkdir objs
cd objs
tar xf ../mlua-objs.tar
cd $HOME
ld -n32 -mips3 -o mlua /usr/lib32/mips3/crt1.o objs/*.o -L/usr/lib32 -lm -lc /usr/lib32/mips3/crtn.o
echo "link status=$status"
ls -l mlua
./mlua microlua-tests/bytecode/hello.lua
echo "hello.lua status=$status"
echo LINK-DONE
