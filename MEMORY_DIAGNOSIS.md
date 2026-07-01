# MicroLua Memory Diagnosis

Baseline date: 2026-06-30

## Method

Lua 5.5.0 was downloaded from `https://www.lua.org/ftp/lua-5.5.0.tar.gz`
and built locally under `bench/memory/lua-5.5.0` with size-oriented flags:

```sh
meson setup build-memory --buildtype=release -Dmemory_diagnostics=true
ninja -C build-memory
python3 bench/memory/build_lua55.py
python3 bench/memory/run.py --mlua build-memory/mlua \
  --lua bench/memory/lua-5.5.0/src/lua \
  --out bench/memory/results/baseline.json
```

The shared workload suite lives in `bench/memory/workloads`. Each script runs
unchanged on both runtimes and must produce byte-identical stdout before any
memory result is accepted.

Lua 5.5 is measured through a custom `lua_newstate` allocator that tracks exact
current heap, peak heap, allocation count, and requested bytes. MicroLua is
measured through diagnostic-only counters enabled by
`-Dmemory_diagnostics=true`. MicroLua snapshots are taken after library init,
after source load/compile, after execution, and after a forced full GC.
Peak RSS is collected per child process with `os.wait4`.

## Baseline Results

| workload | Lua peak RSS | MicroLua peak RSS | Lua heap peak | MicroLua heap peak | Lua retained | MicroLua retained | mlua/lua retained |
|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | 2.06 MiB | 3.33 MiB | 68.8 KiB | 1.50 MiB | 18.8 KiB | 16.1 KiB | 0.86x |
| closures | 2.09 MiB | 1.92 MiB | 63.0 KiB | 103.9 KiB | 18.3 KiB | 16.1 KiB | 0.88x |
| globals_modules | 2.06 MiB | 1.91 MiB | 57.7 KiB | 87.8 KiB | 49.7 KiB | 60.0 KiB | 1.21x |
| knucleotide | 2.19 MiB | 2.03 MiB | 59.6 KiB | 208.8 KiB | 18.3 KiB | 19.1 KiB | 1.05x |
| many_numbers | 2.03 MiB | 1.88 MiB | 46.6 KiB | 52.7 KiB | 18.3 KiB | 16.1 KiB | 0.88x |
| many_short_strings | 2.12 MiB | 1.92 MiB | 77.6 KiB | 104.5 KiB | 24.3 KiB | 31.1 KiB | 1.28x |
| many_tables | 2.11 MiB | 1.95 MiB | 81.4 KiB | 136.0 KiB | 20.3 KiB | 23.1 KiB | 1.14x |
| nested_tables | 2.19 MiB | 2.02 MiB | 120.9 KiB | 200.0 KiB | 18.3 KiB | 16.1 KiB | 0.88x |
| parser_codegen | 1.97 MiB | 1.84 MiB | 23.1 KiB | 25.2 KiB | 18.3 KiB | 16.1 KiB | 0.88x |
| revcomp | 2.36 MiB | 2.16 MiB | 77.4 KiB | 345.4 KiB | 18.6 KiB | 19.1 KiB | 1.03x |
| spectralnorm | 1.97 MiB | 1.86 MiB | 22.7 KiB | 26.7 KiB | 18.3 KiB | 16.1 KiB | 0.88x |
| temp_allocations | 2.16 MiB | 3.16 MiB | 50.1 KiB | 1.32 MiB | 20.3 KiB | 271.1 KiB | 13.37x |

## Confirmed Sources Of Extra Memory

1. **String intern table capacity is retained after temporary unique strings.**
   `temp_allocations` grows the weak intern table to 262,144 bytes. After a
   forced GC, only 64 live strings remain, but the raw string-table backing
   allocation remains live and keeps MicroLua retained heap at 271.1 KiB.

2. **Transient table workloads allocate many header-prefixed raw buffers.**
   `binarytrees` peaks at 1.50 MiB. At peak it has 11,435 table objects
   consuming 914,800 bytes plus 11,464 raw buffers consuming 649,896 bytes.
   This is mostly short-lived allocation churn, not retained live data: after
   forced GC the same workload drops to 16.1 KiB retained heap.

3. **Small tables have a high fixed cost.** On this 64-bit build
   `MLuaGCHeader` is 24 bytes, `MLuaTableHeader` is 56 bytes, and each table
   array/hash backing allocation is another header-prefixed raw object.
   Workloads with many tiny tables therefore pay for table object headers plus
   separate raw allocation headers even when the table stores only a few values.

4. **Prototype and parser memory are not the main retained problem.**
   `parser_codegen` peaks at 25.2 KiB and retains 16.1 KiB after GC. Prototype
   code/constants/line-map capacity is visible but small in this suite.

5. **The GC threshold was tied to total heap capacity, not live data.**
   With a 16 MiB host heap, the initial collection threshold was about 12 MiB.
   Temporary-heavy workloads therefore accumulated dead objects until the end
   even when their live set was small.

6. **Baseline execution reserves dominate tiny scripts but are not excessive.**
   The default state reserves about 7.7 KiB for eval stack, locals, args, and
   frames. This explains why many small MicroLua retained heaps cluster around
   16.1 KiB, but it is already below Lua 5.5 retained heap in several small
   workloads.

## Implemented Reductions

1. **Live-growth GC threshold.** The collection threshold now grows from the
   current live heap by `MLUA_DEFAULT_GC_THRESHOLD_PERCENT`, with a 4 KiB
   minimum, instead of using a percentage of total heap capacity. This reduces
   temporary allocation peaks without changing language behavior.

2. **Post-GC string table shrink.** After dead interned strings are tombstoned,
   MicroLua rehashes live strings into a smaller weak intern table when the old
   table is mostly empty. One immediate cleanup collection removes the old raw
   backing buffer so retained heap reflects the shrink after a forced GC.

3. **Five-byte inline short strings and compact heap string headers.** On
   64-bit builds, short strings now encode an explicit length plus up to five
   bytes in the value word. Heap string headers use `U32` length instead of host
   `Size`, so 6+ byte strings are smaller and 4-5 byte strings avoid the heap
   entirely.

4. **Inline small-table storage.** Tables now keep up to three array entries and
   one hash node inside the table object. External array/hash pointers reuse the
   inline storage after promotion, and table mode flags are packed into the node
   count word. This removes the first raw allocation for common tiny tables
   while keeping the table object at 80 bytes on the measured 64-bit build.

5. **Compact object and closure overhead.** `MLuaGCHeader` now uses a 32-bit
   cached span, shrinking the 64-bit object header from 24 to 16 bytes. Lua
   closures no longer store the unused environment field, and closure/upvalue
   constructors no longer request an extra header-sized payload.

## After Results

Command:

```sh
python3 bench/memory/run.py --mlua build-memory/mlua \
  --lua bench/memory/lua-5.5.0/src/lua \
  --out bench/memory/results/after.json
```

| workload | Lua peak RSS | MicroLua peak RSS | Lua heap peak | MicroLua heap peak | Lua retained | MicroLua retained | mlua/lua retained |
|---|---:|---:|---:|---:|---:|---:|---:|
| binarytrees | 2.08 MiB | 1.89 MiB | 68.8 KiB | 43.5 KiB | 18.8 KiB | 13.4 KiB | 0.71x |
| closures | 2.09 MiB | 1.89 MiB | 63.0 KiB | 62.7 KiB | 18.3 KiB | 13.4 KiB | 0.73x |
| globals_modules | 2.06 MiB | 1.89 MiB | 57.7 KiB | 52.6 KiB | 49.7 KiB | 43.9 KiB | 0.88x |
| knucleotide | 2.19 MiB | 1.88 MiB | 59.6 KiB | 36.2 KiB | 18.3 KiB | 13.9 KiB | 0.76x |
| many_numbers | 2.03 MiB | 1.88 MiB | 46.6 KiB | 32.2 KiB | 18.3 KiB | 13.4 KiB | 0.73x |
| many_short_strings | 2.09 MiB | 1.88 MiB | 77.6 KiB | 32.2 KiB | 24.3 KiB | 13.4 KiB | 0.55x |
| many_tables | 2.12 MiB | 1.89 MiB | 81.4 KiB | 57.9 KiB | 20.3 KiB | 13.4 KiB | 0.66x |
| nested_tables | 2.19 MiB | 1.95 MiB | 120.9 KiB | 118.9 KiB | 18.3 KiB | 13.4 KiB | 0.73x |
| parser_codegen | 1.97 MiB | 1.86 MiB | 23.1 KiB | 20.4 KiB | 18.3 KiB | 13.4 KiB | 0.73x |
| revcomp | 2.36 MiB | 1.88 MiB | 77.4 KiB | 60.4 KiB | 18.6 KiB | 13.9 KiB | 0.75x |
| spectralnorm | 1.97 MiB | 1.86 MiB | 22.7 KiB | 20.7 KiB | 18.3 KiB | 13.4 KiB | 0.73x |
| temp_allocations | 2.12 MiB | 1.86 MiB | 50.1 KiB | 31.2 KiB | 20.3 KiB | 13.4 KiB | 0.66x |

| workload | MicroLua peak before | MicroLua peak after | peak change | retained before | retained after |
|---|---:|---:|---:|---:|---:|
| binarytrees | 1540.5 KiB | 43.5 KiB | -97.2% | 16.1 KiB | 13.4 KiB |
| closures | 103.9 KiB | 62.7 KiB | -39.6% | 16.1 KiB | 13.4 KiB |
| globals_modules | 87.8 KiB | 52.6 KiB | -40.1% | 60.0 KiB | 43.9 KiB |
| knucleotide | 208.8 KiB | 36.2 KiB | -82.6% | 19.1 KiB | 13.9 KiB |
| many_numbers | 52.7 KiB | 32.2 KiB | -38.9% | 16.1 KiB | 13.4 KiB |
| many_short_strings | 104.5 KiB | 32.2 KiB | -69.1% | 31.1 KiB | 13.4 KiB |
| many_tables | 136.0 KiB | 57.9 KiB | -57.4% | 23.1 KiB | 13.4 KiB |
| nested_tables | 200.0 KiB | 118.9 KiB | -40.5% | 16.1 KiB | 13.4 KiB |
| parser_codegen | 25.2 KiB | 20.4 KiB | -19.1% | 16.1 KiB | 13.4 KiB |
| revcomp | 345.4 KiB | 60.4 KiB | -82.5% | 19.1 KiB | 13.9 KiB |
| spectralnorm | 26.7 KiB | 20.7 KiB | -22.5% | 16.1 KiB | 13.4 KiB |
| temp_allocations | 1354.6 KiB | 31.2 KiB | -97.7% | 271.1 KiB | 13.4 KiB |

## Current Status

MicroLua is now lower than Lua 5.5 on measured peak heap, retained heap, and
peak RSS for every workload in this suite. The closest peak-heap cases are
`closures` (62.7 KiB vs Lua's 63.0 KiB) and `nested_tables` (118.9 KiB vs
Lua's 120.9 KiB), so future object-header or upvalue changes should keep these
workloads in the acceptance set.

The separate Benchmark Game-derived `revcomp` CLI high-water workload now uses
less MicroLua heap than Lua 5.5 (104.5 KiB vs 115.4 KiB, down from 144.3 KiB).
The confirmed cause was table-builder lifetime and capacity slack: the first
builder stayed reachable through a dead local, and large append-only arrays grew
to 4096 slots for 3600-3660 live elements. The fix combines conservative
last-use local clearing, lexical scope cleanup, a reload-safe large
`table.concat` collection point, and tighter large-array growth.

## Deferred Reduction Plan

1. Trim prototype buffers after successful source compile/bytecode load.
   Expected impact: modest reduction in parser/codegen-heavy scripts; low risk
   because execution uses `CodeSize`, `ConstantsSize`, and metadata sizes.

2. Investigate single-owner closed upvalues only if closure-heavy workloads
   regress. The current compact header and allocation fix are enough for this
   suite, so a more complex upvalue representation is not justified yet.

3. Keep the table inline capacities workload-tested. Raising them would help
   larger tiny-table constructors but can regress empty or array-only tables.

4. Revisit specialized packed arrays only if future workloads show large dense
   builders still dominate peak heap after the tighter growth policy.

## Risks

- Shrinking the string table must preserve open-addressing probe chains and weak
  tombstone behavior. Rehashing live entries into a fresh table is safer than
  compacting in place.
- Inline table storage stores external buffer pointers inside the inline slots
  after promotion. GC mark/update code must always use the table accessors, not
  raw struct field assumptions.
- The compact GC header limits a single object span to 32 bits. This matches
  MicroLua's tiny-heap goals, but oversized allocation attempts must continue to
  fail deterministically.
- Prototype trimming allocates replacement raw buffers. It must keep the
  original buffers alive until the new compact copies are installed and must be
  tested with GC compaction, nested functions, line info, and bytecode dump/load.
