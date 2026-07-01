# Benchmark Game-derived workloads

This directory contains small standalone MicroLua-compatible ports of selected
Lua programs from the Computer Language Benchmarks Game:

- `binarytrees-lua-4`: allocation churn
- `knucleotide-lua-2`: substring/table pressure
- `revcomp-lua-5`: string-builder pressure
- `spectralnorm-lua-1`: numeric arrays

The original downloaded HTML source pages are kept in `original/` for
traceability. The runnable files in `workloads/` are scaled down for local runs
and avoid unsupported MicroLua features such as metatables, generated `load`,
stdin-driven harnesses, and external C modules.

Run:

```sh
python3 bench/benchmarksgame/bench.py
python3 bench/benchmarksgame/bench.py --mlua builddir-release/mlua --lua /path/to/lua5.5
```

The harness compares stdout byte-for-byte before reporting memory numbers. Lua
memory is exact peak Lua heap from a tracking `lua_newstate` allocator.
MicroLua memory is exact constrained-heap high-water from a high-limit `--dump`
run. The comparison intentionally reports memory pressure only, not interpreter
speed.
