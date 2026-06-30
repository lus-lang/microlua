-- _lua_mem.lua: run a target script under reference Lua and report its PEAK
-- heap occupancy in bytes on stderr (so it never mingles with the target's
-- stdout). Peak is sampled non-invasively with a count hook, so the workload
-- file is run unmodified. Invoked by bench.py as:
--     lua5.5 bench/_lua_mem.lua <target.lua>
local target = assert(arg[1], "usage: _lua_mem.lua <target.lua>")
local chunk = assert(loadfile(target))

local peak = 0
local function sample()
  local kb = collectgarbage("count")
  if kb > peak then peak = kb end
end

-- Fire every 1000 VM instructions; frequent enough to catch the high-water of
-- the workload's allocation while it is still live.
debug.sethook(sample, "", 1000)
chunk()
debug.sethook()
sample()

io.stderr:write(string.format("__BENCH_MEM__ %d\n", math.floor(peak * 1024)))
