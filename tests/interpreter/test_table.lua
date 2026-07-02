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

assert(test.run())
