---
trigger: always_on
---

This is MicroLua, _not_ vanilla Lua. We have our own runtime implementation. We only care about language semantics, not fidelity to the original Lua codebase. Therefore, you cannot always assume that standard internal Lua constructs will work in MicroLua, nor that MicroLua SHOULD receive equivalent constructs. Instead, think of constructs that are appropriate to MicroLua's mission. Here are MicroLua's core aspects:

- Provides most of Lua within a tiny footprint.
- No dependence on the C standard library.
- No dynamic memory allocation, pass your own pre-allocated heap to the interpreter[^1].
- Values are NaN-boxed on 64-bit platforms and alignment-tagged on 32-bit platforms.
- Interpreter is implemented as a stack machine, with instructions encoded as single or double bytes.
- Single-pass Pratt parsing and code generation with minimal resource cost across source→bytecode pipeline.
- Lisp-2 mark-compact garbage collection that reclaims and defragments the heap to conserve memory.
- String and scripts are encoded as UTF-8, with all string operations being Unicode-aware.
- Holes in table arrays are runtime errors to ensure heap compactness.

Additional implementation inforrmation can be found under /spec (SPEC.EARLY.MD, SPEC.BROAD.md, SPEC.TECHNICAL.md). Refresh your memory now and then by pulling and re-reading these files, however take note that these specs have been the initial versions and we have elected to make some modifications along the way during implementation -- they should be inspirations and indicators, not hard constraints.
