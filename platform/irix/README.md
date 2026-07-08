# MicroLua on SGI IRIX 6.5 (mips3 / n32 / big-endian)

Reference machine: SGI Indigo2 (IP22, R4400 @ 150 MHz, IRIX 6.5.22f). The
port preset is `src/ports/irix_mips.h` (32-bit value word → alignment-tag
representation, 8-byte GC alignment, computed-goto dispatch). Big-endian
core behavior is separately covered without hardware by
`cross/mips-be-qemu.ini` (or `tools/mips_be_docker.sh` on macOS), and the
`canary_irix` guard test compiles every core TU whenever the cross gcc is
on PATH.

## Why the build is split across two machines

- **IRIX boxes usually have no compiler**: MIPSpro is licensed and its
  backend subsystems are often not installed, so compilation is cross from
  a modern host.
- Modern gcc/binutils dropped the `mips-sgi-irix6.5` target. The last
  workable combo is **binutils 2.24 + gcc 4.7.4** (`--enable-obsolete`),
  built inside a `debian:jessie` amd64 container: `Dockerfile.toolchain` +
  `build-toolchain.sh`, against a sysroot pulled from the box
  (`/usr/include`, `/lib32`, `/usr/lib32` incl. `mips3/crt*.o`,
  `libc.so.1`, `libm.so`). Beware pulling a sysroot onto case-insensitive
  macOS filesystems: IRIX ships both `libc.so` and `libC.so`.
- **GNU ld output does not run on IRIX.** Verified empirically on the
  Indigo2: rld segfaults parsing GNU ld's dynamic layout before loading a
  single dependency, and GNU ld mis-resolves MIPSpro composite relocations
  (GPREL16+SUB+HI16) in static archives like `libm.a`, producing wild
  addresses. This matches the historical record — IRIX gcc setups always
  used GNU **as** with the **native SGI ld**.
- So: **compile to .o with the cross gcc, link on the box with native ld**
  (`ld -n32 -mips3 crt1.o *.o -lm -lc crtn.o`).

## Native-ld section hygiene (required)

SGI ld (ld32) aborts with `Internal: at ../../ld/write.c writing beyond
end-of-file` when fed objects with post-1998 GNU section conventions.
`build-objects.sh` therefore compiles with

```
-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-merge-constants
-fno-reorder-functions -fno-ident -fno-dwarf2-cfi-asm
```

and objcopy-strips `.debug_frame .comment .pdr .mdebug`, leaving plain
`.text/.data/.bss/.rodata/.reginfo` objects the native linker accepts.

## Pipeline

1. `build-objects.sh` (inside the `irix-cross` Docker image) →
   `builddir-irix/mlua-objs.tar`.
2. Transfer the tar + `link-on-box.csh` to the box. FTP is the dependable
   channel; telnet from modern hosts can drop intermittently, so run long
   jobs as `nohup csh script >& out.txt &` and poll the output file over
   FTP.
3. On the box: `csh link-on-box.csh` → links `mlua` against the native
   libc/libm and smoke-runs it.

## Verified on hardware (2026-07-07)

Full interpreter + smoke suites pass on the Indigo2, including the
cross-endian bytecode path: `.mlu` compiled on a little-endian host runs
on the big-endian box (and vice versa) through `MLuaUndump`'s endianness
handling.
