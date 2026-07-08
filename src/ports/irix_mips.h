/*
 * SGI IRIX 6.5 / MIPS n32 MicroLua port preset.
 *
 * Target: big-endian MIPS III (R4400-class workstation) under IRIX 6.5,
 * n32 ABI as produced by a mips-sgi-irix6.5 gcc: 32-bit int/long/pointers,
 * IEEE binary64 double in hardware. The 32-bit value word selects the
 * alignment-tagging representation. Workstation-class RAM (tens to hundreds
 * of MB), so the default arenas and GC pacing stay.
 *
 * Build pipeline (cross gcc objects + native SGI ld link): platform/irix/.
 */

#ifndef MLUA_PORT_IRIX_MIPS_H
#define MLUA_PORT_IRIX_MIPS_H

#define MLUA_PTR_SIZE 4  /* tagging representation (32-bit value word) */
#define MLUA_ALIGNMENT 8 /* allocator-enforced; frees low 3 bits for tags */

/* MIPS III traps on misaligned accesses, so payload fields must stay
 * naturally aligned: MLUA_GC_HEADER_ALIGN keeps its default. */

/* The IRIX-capable gcc (4.7, see platform/irix/) supports labels-as-values,
 * and a workstation has no image-size pressure: buy the dispatch speed. */
#define MLUA_VM_COMPUTED_GOTO 1

#endif /* MLUA_PORT_IRIX_MIPS_H */
