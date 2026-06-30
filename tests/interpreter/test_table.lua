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

assert(test.run())
