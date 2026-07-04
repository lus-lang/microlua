--[[
  MicroLua Table Library Tests
  /tests/interpreter/test_table.lua
]]

local test = require("_base")

test.describe("table.sort", function()
    test.it("sorts numbers ascending", function()
        local t = { 3, 1, 4, 1, 5 }
        table.sort(t)
        test.expect(t[1]).toBe(1)
        test.expect(t[2]).toBe(1)
        test.expect(t[3]).toBe(3)
        test.expect(t[4]).toBe(4)
        test.expect(t[5]).toBe(5)
    end)

    test.it("sorts strings", function()
        local t = { "banana", "apple", "cherry" }
        table.sort(t)
        test.expect(t[1]).toBe("apple")
        test.expect(t[2]).toBe("banana")
        test.expect(t[3]).toBe("cherry")
    end)

    test.it("handles empty table", function()
        local t = {}
        table.sort(t)
        test.expect(#t).toBe(0)
    end)

    test.it("handles single element", function()
        local t = { 42 }
        table.sort(t)
        test.expect(t[1]).toBe(42)
    end)

    test.it("sorts an already-sorted 10k array", function()
        -- Worst case for a last-element-pivot scheme; median-of-3 keeps it
        -- O(n log n) so this also guards against quadratic hangs.
        local t = {}
        for i = 1, 10000 do t[i] = i end
        table.sort(t)
        for i = 1, 10000 do
            if t[i] ~= i then test.expect(t[i]).toBe(i) end
        end
        test.expect(t[10000]).toBe(10000)
    end)

    test.it("sorts a reverse-sorted 10k array", function()
        local t = {}
        for i = 1, 10000 do t[i] = 10001 - i end
        table.sort(t)
        for i = 1, 10000 do
            if t[i] ~= i then test.expect(t[i]).toBe(i) end
        end
        test.expect(t[1]).toBe(1)
    end)

    test.it("sorts an all-equal 5k array", function()
        -- Hoare partitioning splits equal runs evenly; Lomuto degrades to
        -- O(n^2) here.
        local t = {}
        for i = 1, 5000 do t[i] = 7 end
        table.sort(t)
        test.expect(t[1]).toBe(7)
        test.expect(t[5000]).toBe(7)
    end)

    test.it("sorts 30k random elements and keeps them a permutation", function()
        -- Sized well past what the old fixed 64-entry range stack could
        -- hold when it pushed both sides: it silently DROPPED ranges and
        -- returned unsorted data.
        local t = {}
        local seed = 1
        local sum = 0
        for i = 1, 30000 do
            seed = (seed * 1103 + 12345) % 100003
            t[i] = seed
            sum = sum + seed
        end
        table.sort(t)
        local after = 0
        for i = 1, 30000 do
            after = after + t[i]
            if i > 1 and t[i - 1] > t[i] then
                test.expect(t[i - 1] <= t[i]).toBeTrue()
            end
        end
        test.expect(after).toBe(sum)
    end)

    test.it("supports a custom descending comparator", function()
        local t = { 3, 1, 4, 1, 5, 9, 2, 6 }
        table.sort(t, function(a, b) return a > b end)
        test.expect(t[1]).toBe(9)
        test.expect(t[8]).toBe(1)
        for i = 2, 8 do
            test.expect(t[i - 1] >= t[i]).toBeTrue()
        end
    end)

    test.it("propagates comparator errors", function()
        local t = {}
        for i = 1, 100 do t[i] = i * 3 % 17 end
        local ok = pcall(table.sort, t, function() error("boom") end)
        test.expect(ok).toBe(false)
    end)

    test.it("stays memory-safe with an inconsistent comparator", function()
        local t = {}
        for i = 1, 500 do t[i] = i end
        -- A lying comparator may not produce an order, but must not crash
        -- or lose elements.
        pcall(table.sort, t, function() return true end)
        local sum = 0
        for i = 1, 500 do sum = sum + t[i] end
        test.expect(sum).toBe(125250)
    end)
end)

test.describe("table.concat", function()
    test.it("concatenates strings", function()
        local t = { "a", "b", "c" }
        test.expect(table.concat(t)).toBe("abc")
    end)

    test.it("uses separator", function()
        local t = { "a", "b", "c" }
        test.expect(table.concat(t, ", ")).toBe("a, b, c")
    end)

    test.it("handles empty table", function()
        test.expect(table.concat({})).toBe("")
    end)

    test.it("does not truncate results larger than 4KB", function()
        local t = {}
        for i = 1, 2000 do t[i] = "abcde" end   -- 2000*5 = 10000 bytes
        local r = table.concat(t)
        test.expect(#r).toBe(10000)
        test.expect(string.sub(r, 1, 5)).toBe("abcde")
        test.expect(string.sub(r, -5)).toBe("abcde")
    end)

    test.it("leaves large input tables reusable", function()
        local t = {}
        for i = 1, 600 do t[i] = "xy" end
        test.expect(#table.concat(t)).toBe(1200)
        t[601] = "z"
        test.expect(#t).toBe(601)
        test.expect(t[1]).toBe("xy")
        test.expect(t[601]).toBe("z")
    end)

    test.it("places separators only between elements (with a range)", function()
        local t = { "a", "b", "c", "d", "e" }
        test.expect(table.concat(t, "-", 2, 4)).toBe("b-c-d")  -- no leading/trailing sep
    end)

    test.it("returns empty for an inverted range", function()
        local t = { "a", "b", "c" }
        test.expect(table.concat(t, ",", 3, 1)).toBe("")
    end)
end)

test.describe("table.insert", function()
    test.it("appends to end", function()
        local t = { 1, 2 }
        table.insert(t, 3)
        test.expect(#t).toBe(3)
        test.expect(t[3]).toBe(3)
    end)

    test.it("inserts at position", function()
        local t = { 1, 3 }
        table.insert(t, 2, 2)
        test.expect(t[2]).toBe(2)
        test.expect(t[3]).toBe(3)
    end)
end)

test.describe("table.remove", function()
    test.it("removes last element", function()
        local t = { 1, 2, 3 }
        local v = table.remove(t)
        test.expect(v).toBe(3)
        test.expect(#t).toBe(2)
    end)

    test.it("removes at position", function()
        local t = { 1, 2, 3 }
        local v = table.remove(t, 2)
        test.expect(v).toBe(2)
        test.expect(t[2]).toBe(3)
    end)
end)

test.describe("table holes", function()
    test.it("rejects an assignment that would create a hole", function()
        local t = { 1 }
        local ok = pcall(function() t[3] = 30 end)
        test.expect(ok).toBeFalse()
        test.expect(t[3]).toBeNil()
        test.expect(#t).toBe(1)
    end)

    test.it("allows filling the next slot, then the one after", function()
        local t = { 1 }
        t[2] = 2
        t[3] = 3
        test.expect(#t).toBe(3)
        test.expect(t[3]).toBe(3)
    end)

    test.it("treats a nil assignment past the end as a no-op", function()
        local t = { 1 }
        local ok = pcall(function() t[5] = nil end)
        test.expect(ok).toBeTrue()
        test.expect(#t).toBe(1)
    end)

    test.it("errors when table.insert position is out of bounds", function()
        local t = { 1, 2, 3 }
        local ok = pcall(function() table.insert(t, 10, 99) end)
        test.expect(ok).toBeFalse()
        test.expect(#t).toBe(3)
    end)

    test.it("still inserts at the valid boundary position", function()
        local t = { 1, 2, 3 }
        table.insert(t, 4, 4)
        test.expect(#t).toBe(4)
        test.expect(t[4]).toBe(4)
    end)
end)

-- Indexed reads/writes with in-range integer keys take a direct array-slot
-- path in the VM; these pin every precondition edge of that path against
-- the generic route's semantics.
test.describe("array-window access edges", function()
    test.it("reads and writes at both ends of the array window", function()
        local t = {}
        for i = 1, 40 do t[i] = i * 3 end
        test.expect(t[1]).toBe(3)
        test.expect(t[40]).toBe(120)
        t[1] = -1
        t[40] = -40
        test.expect(t[1]).toBe(-1)
        test.expect(t[40]).toBe(-40)
        test.expect(#t).toBe(40)
    end)

    test.it("append at len+1 still extends the length", function()
        local t = { 10, 20 }
        t[3] = 30
        test.expect(#t).toBe(3)
        test.expect(t[3]).toBe(30)
    end)

    test.it("reads past the length return nil", function()
        local t = { 1, 2 }
        test.expect(t[3]).toBeNil()
        test.expect(t[100]).toBeNil()
        test.expect(t[0]).toBeNil()
        test.expect(t[-1]).toBeNil()
    end)

    test.it("nil store at the end shrinks, mid-array leaves a nil slot",
            function()
        local t = { 1, 2, 3 }
        t[3] = nil
        test.expect(#t).toBe(2)
        local u = { 1, 2, 3, 4 }
        u[2] = nil
        test.expect(u[2]).toBeNil()
        test.expect(u[3]).toBe(3)
        test.expect(#u).toBe(4)
    end)

    test.it("nil mid-array slot delegates through the forward chain",
            function()
        local base = { 111, 222 }
        local t = { 1, 2, 3 }
        table.forward(t, base)
        test.expect(t[2]).toBe(2)
        t[2] = nil
        -- the nil slot must fall through to the forward table, exactly
        -- like the generic read path
        test.expect(t[2]).toBe(222)
    end)

    test.it("float literal keys never touch the array part", function()
        -- (Float keys go to the hash; their round-trip readability is
        -- representation-dependent -- boxed floats hash by identity on
        -- the 32-bit path -- so only the array part's isolation is
        -- portable and pinned here.)
        local t = { 7, 8, 9 }
        t[2.0] = 80
        test.expect(t[2]).toBe(8)
        test.expect(#t).toBe(3)
    end)

    test.it("string keys on a table with an array part use the hash",
            function()
        local t = { 1, 2, 3 }
        t.x = "side"
        test.expect(t.x).toBe("side")
        test.expect(t[1]).toBe(1)
        test.expect(#t).toBe(3)
    end)
end)

test.describe("integer arithmetic and comparison edges", function()
    test.it("arithmetic near the 32-bit boundary stays exact", function()
        local big = 2000000000
        test.expect(big + 100000000).toBe(2100000000)
        test.expect(-big - 100000000).toBe(-2100000000)
        test.expect(1000000 * 2000).toBe(2000000000)
    end)

    test.it("comparisons across wide magnitudes", function()
        test.expect(2000000000 > 1999999999).toBeTrue()
        test.expect(-2000000000 < 2000000000).toBeTrue()
        test.expect(268435456 == 268435456).toBeTrue()  -- above 2^28
        test.expect(268435455 ~= 268435456).toBeTrue()
    end)

    test.it("int/float mixed comparison still crosses the divide", function()
        test.expect(5 == 5.0).toBeTrue()
        test.expect(5 < 5.5).toBeTrue()
        test.expect(6.0 <= 6).toBeTrue()
    end)
end)

test.describe("positional insert/remove shifting", function()
    test.it("insert at head, middle, and end", function()
        local t = { "b", "d" }
        table.insert(t, 1, "a")
        table.insert(t, 3, "c")
        table.insert(t, "e")
        test.expect(table.concat(t)).toBe("abcde")
        test.expect(#t).toBe(5)
    end)

    test.it("remove from head, middle, and end", function()
        local t = { "a", "b", "c", "d", "e" }
        test.expect(table.remove(t, 1)).toBe("a")
        test.expect(table.remove(t, 2)).toBe("c")
        test.expect(table.remove(t)).toBe("e")
        test.expect(table.concat(t)).toBe("bd")
        test.expect(#t).toBe(2)
    end)

    test.it("interleaved churn stays consistent", function()
        local t = {}
        for i = 1, 50 do table.insert(t, 1, i) end -- 50..1
        test.expect(t[1]).toBe(50)
        test.expect(t[50]).toBe(1)
        for i = 1, 25 do table.remove(t, 1) end
        test.expect(t[1]).toBe(25)
        test.expect(#t).toBe(25)
        local sum = 0
        for _, v in ipairs(t) do sum = sum + v end
        test.expect(sum).toBe(325) -- 1+..+25
    end)

    test.it("out-of-bounds insert positions raise", function()
        test.expect(pcall(table.insert, { 1 }, 5, "x")).toBe(false)
        test.expect(pcall(table.insert, { 1 }, 0, "x")).toBe(false)
    end)
end)

test.describe("large array append", function()
    test.it("fills 200k sequential elements correctly", function()
        -- Regression canary for growth policy: flat (+256) growth made this
        -- quadratic (~800 reallocations); geometric growth does ~15.
        local t = {}
        for i = 1, 200000 do t[i] = i * 2 - 1 end
        test.expect(#t).toBe(200000)
        test.expect(t[1]).toBe(1)
        test.expect(t[100000]).toBe(199999)
        test.expect(t[200000]).toBe(399999)
        local sum = 0
        for i = 199990, 200000 do sum = sum + t[i] end
        test.expect(sum).toBe(4399879)
    end)

    test.it("append stays correct across the 1024-slot policy boundary", function()
        local t = {}
        for i = 1, 3000 do t[i] = i end
        for i = 1, 3000 do
            if t[i] ~= i then test.expect(t[i]).toBe(i) end
        end
        test.expect(#t).toBe(3000)
    end)
end)

test.describe("hash deletion", function()
    test.it("keeps other keys reachable after a delete", function()
        -- Regression: nilling a deleted node's Key terminated probe chains
        -- early and orphaned every later same-chain entry.
        local t = {}
        for i = 1, 200 do t["a" .. i] = i end
        for i = 1, 200, 2 do t["a" .. i] = nil end
        for i = 2, 200, 2 do
            test.expect(t["a" .. i]).toBe(i)
        end
        for i = 1, 200, 2 do
            test.expect(t["a" .. i]).toBe(nil)
        end
    end)

    test.it("reinserting a deleted key resurrects it", function()
        local t = {}
        for i = 1, 50 do t["k" .. i] = i end
        for i = 1, 50 do t["k" .. i] = nil end
        for i = 1, 50 do t["k" .. i] = i * 10 end
        for i = 1, 50 do
            test.expect(t["k" .. i]).toBe(i * 10)
        end
    end)

    test.it("delete/reinsert cycles do not balloon the table", function()
        -- Dead nodes must be reclaimed on rebuild, not accumulate forever.
        local t = {}
        for cycle = 1, 100 do
            for i = 1, 20 do t["c" .. i] = cycle end
            for i = 1, 20 do t["c" .. i] = nil end
        end
        local n = 0
        for _ in pairs(t) do n = n + 1 end
        test.expect(n).toBe(0)
    end)

    test.it("next() sees exactly the live keys after deletes", function()
        local t = {}
        for i = 1, 60 do t["n" .. i] = i end
        for i = 1, 60, 3 do t["n" .. i] = nil end
        local count, sum = 0, 0
        for k, v in pairs(t) do
            count = count + 1
            sum = sum + v
        end
        local expectCount, expectSum = 0, 0
        for i = 1, 60 do
            if i % 3 ~= 1 then
                expectCount = expectCount + 1
                expectSum = expectSum + i
            end
        end
        test.expect(count).toBe(expectCount)
        test.expect(sum).toBe(expectSum)
    end)

    test.it("deleting the current key during pairs keeps iterating", function()
        local t = {}
        for i = 1, 40 do t["p" .. i] = i end
        local seen = 0
        for k in pairs(t) do
            seen = seen + 1
            t[k] = nil -- delete the key we are standing on
        end
        test.expect(seen).toBe(40)
        for i = 1, 40 do
            test.expect(t["p" .. i]).toBe(nil)
        end
    end)

    test.it("erased key falls through to the forward table", function()
        local base = { shadowed = "base" }
        local t = { shadowed = "own" }
        table.forward(t, base)
        test.expect(t.shadowed).toBe("own")
        t.shadowed = nil
        test.expect(t.shadowed).toBe("base")
    end)

    test.it("integer hash keys survive same-chain deletes", function()
        -- Positive int keys live in the array part (holes are rejected), so
        -- negative keys exercise the hash part's integer chains instead.
        local t = {}
        for i = 1000, 1512 do t[-i * 7] = i end
        for i = 1000, 1512, 2 do t[-i * 7] = nil end
        for i = 1001, 1512, 2 do
            test.expect(t[-i * 7]).toBe(i)
        end
    end)
end)

test.describe("sequential append fast path", function()
    -- Pins the VM's no-growth append arm in TryArraySetFast (t[i] = v with
    -- i == #t + 1 and spare capacity, through the locals-indexed
    -- SETTABLE_LL shape): appends, overwrites, #t bookkeeping, and the
    -- hole rule must all behave exactly like the generic path.
    test.it("interleaves appends, overwrites and length reads", function()
        local t = {}
        for i = 1, 100 do
            t[i] = i * 7
            test.expect(#t).toBe(i)
            if i % 10 == 0 then
                t[i / 2 + 1] = -1 -- overwrite inside the window (i is even: exact int)
            end
        end
        test.expect(#t).toBe(100)
        test.expect(t[100]).toBe(700)
        test.expect(t[6]).toBe(-1)
        test.expect(t[99]).toBe(99 * 7)
    end)

    test.it("still rejects holes past the append point", function()
        local t = {}
        for i = 1, 5 do t[i] = i end
        local ok = pcall(function() t[100] = true end)
        test.expect(ok).toBeFalse()
        test.expect(#t).toBe(5)
    end)

    test.it("appends across the geometric growth boundary", function()
        -- growth is x1.5 above 1024: cross several boundaries and verify
        -- contents (growth iterations take the slow path, the rest the
        -- fast arm; the two must interleave seamlessly)
        local t = {}
        local n = 3000
        for i = 1, n do t[i] = n - i end
        test.expect(#t).toBe(n)
        local sum = 0
        for i = 1, n do sum = sum + (t[i] - (n - i)) end
        test.expect(sum).toBe(0)
    end)
end)

assert(test.run())
