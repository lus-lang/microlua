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

assert(test.run())
