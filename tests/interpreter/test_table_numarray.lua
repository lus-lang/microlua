--[[
  MicroLua Typed Numeric Array Tests
  /tests/interpreter/test_table_numarray.lua

  Exercises the shapes MLUA_TABLE_NUM_ARRAYS changes internally. Every
  assertion here must hold with the knob ON and OFF: typed arrays are a
  representation, not a semantics. (One deliberate exception is documented
  with the knob: tostring of a STORED integral float literal like 2.0 may
  canonicalize to "2", exactly as dump/undump already canonicalizes such
  constants - so nothing below pins that.)
]]

local test = require("_base")

test.describe("float arrays: round-trips", function()
    test.it("stores and reads non-integral floats", function()
        local t = {}
        t[1] = 0.5
        t[2] = 1.25
        t[3] = -3.75
        test.expect(t[1]).toBe(0.5)
        test.expect(t[2]).toBe(1.25)
        test.expect(t[3]).toBe(-3.75)
        test.expect(#t).toBe(3)
    end)

    test.it("constructor-built float arrays behave identically", function()
        local t = { 0.5, 1.5, 2.5 }
        test.expect(#t).toBe(3)
        test.expect(t[2]).toBe(1.5)
    end)

    test.it("accepts exact integers among floats", function()
        local t = { 0.5 }
        t[2] = 3
        t[3] = -100
        test.expect(t[2]).toBe(3)
        test.expect(t[2] == 3).toBeTrue()
        test.expect(tostring(t[2])).toBe("3")
        test.expect(t[3]).toBe(-100)
        test.expect(#t).toBe(3)
    end)

    test.it("preserves values through arithmetic reads", function()
        local t = { 0.5, 1.5, 2.5, 3.5 }
        local s = 0
        for i = 1, #t do
            s = s + t[i]
        end
        test.expect(s).toBe(8)
    end)

    test.it("large integers keep their exact value", function()
        -- 2^24 + 1 is inexact in binary32: ports with MLUA_FLOAT=float must
        -- not lose it (typed arrays demote rather than round).
        local t = { 0.5 }
        t[2] = 16777217
        test.expect(t[2]).toBe(16777217)
        test.expect(t[1]).toBe(0.5)
    end)
end)

test.describe("float arrays: growth and length", function()
    test.it("appends past the initial capacity", function()
        local t = {}
        for i = 1, 100 do
            t[i] = i + 0.5
        end
        test.expect(#t).toBe(100)
        test.expect(t[1]).toBe(1.5)
        test.expect(t[64]).toBe(64.5)
        test.expect(t[100]).toBe(100.5)
    end)

    test.it("grows past 1024 (linear growth regime)", function()
        local t = {}
        for i = 1, 1200 do
            t[i] = i + 0.5
        end
        test.expect(#t).toBe(1200)
        test.expect(t[1024]).toBe(1024.5)
        test.expect(t[1200]).toBe(1200.5)
    end)

    test.it("t[#t + 1] append form works", function()
        local t = { 0.5 }
        t[#t + 1] = 1.5
        t[#t + 1] = 2.5
        test.expect(#t).toBe(3)
        test.expect(t[3]).toBe(2.5)
    end)

    test.it("nil at the end shrinks the length", function()
        local t = { 0.5, 1.5, 2.5 }
        t[3] = nil
        test.expect(#t).toBe(2)
        test.expect(t[3]).toBeNil()
        test.expect(t[2]).toBe(1.5)
    end)

    test.it("nil one past the end is a no-op", function()
        local t = { 0.5 }
        t[2] = nil
        test.expect(#t).toBe(1)
    end)
end)

test.describe("float arrays: demotion edges", function()
    test.it("string store preserves all prior floats", function()
        local t = { 0.5, 1.5, 2.5, 3.5 }
        t[2] = "two"
        test.expect(t[1]).toBe(0.5)
        test.expect(t[2]).toBe("two")
        test.expect(t[3]).toBe(2.5)
        test.expect(t[4]).toBe(3.5)
        test.expect(#t).toBe(4)
    end)

    test.it("table store preserves all prior floats", function()
        local t = { 0.5, 1.5 }
        t[1] = { nested = true }
        test.expect(t[1].nested).toBeTrue()
        test.expect(t[2]).toBe(1.5)
    end)

    test.it("interior nil behaves like a generic array", function()
        local t = { 0.5, 1.5, 2.5 }
        t[2] = nil
        test.expect(t[2]).toBeNil()
        test.expect(t[1]).toBe(0.5)
        test.expect(t[3]).toBe(2.5)
        test.expect(#t).toBe(3)
    end)

    test.it("floats keep working after demotion", function()
        local t = { 0.5, 1.5 }
        t[1] = "x"
        t[3] = 9.5
        test.expect(t[3]).toBe(9.5)
        t[1] = 0.25
        test.expect(t[1]).toBe(0.25)
    end)
end)

test.describe("float arrays: hole errors", function()
    test.it("sparse writes raise the standard hole error", function()
        local t = { 0.5, 1.5 }
        local ok1, err1 = pcall(function() t[10] = 9.5 end)
        local u = { 1, 2 }
        local ok2, err2 = pcall(function() u[10] = 9 end)
        test.expect(ok1).toBeFalse()
        test.expect(ok2).toBeFalse()
        test.expect(tostring(err1)).toBe(tostring(err2))
    end)

    test.it("nil to an absent sparse index is a no-op", function()
        local t = { 0.5 }
        local ok = pcall(function() t[10] = nil end)
        test.expect(ok).toBeTrue()
        test.expect(#t).toBe(1)
    end)
end)

test.describe("float arrays: iteration", function()
    test.it("ipairs walks every element", function()
        local t = { 0.5, 1.5, 2.5 }
        local n, s = 0, 0
        for i, v in ipairs(t) do
            n = n + 1
            s = s + v
        end
        test.expect(n).toBe(3)
        test.expect(s).toBe(4.5)
    end)

    test.it("pairs covers array and hash parts", function()
        local t = { 0.5, 1.5 }
        t.x = "y"
        local count, sum, sawX = 0, 0, false
        for k, v in pairs(t) do
            count = count + 1
            if k == "x" then
                sawX = (v == "y")
            else
                sum = sum + v
            end
        end
        test.expect(count).toBe(3)
        test.expect(sum).toBe(2)
        test.expect(sawX).toBeTrue()
    end)

    test.it("next() steps through a float array", function()
        local t = { 0.5, 1.5 }
        local k1, v1 = next(t)
        test.expect(k1).toBe(1)
        test.expect(v1).toBe(0.5)
        local k2, v2 = next(t, k1)
        test.expect(k2).toBe(2)
        test.expect(v2).toBe(1.5)
        test.expect(next(t, k2)).toBeNil()
    end)
end)

test.describe("float arrays: table library", function()
    test.it("table.insert and table.remove", function()
        local t = { 0.5, 2.5 }
        table.insert(t, 2, 1.5)
        test.expect(#t).toBe(3)
        test.expect(t[2]).toBe(1.5)
        local removed = table.remove(t, 1)
        test.expect(removed).toBe(0.5)
        test.expect(t[1]).toBe(1.5)
        test.expect(#t).toBe(2)
    end)

    test.it("table.sort ascending and with a comparator", function()
        local t = { 3.5, 1.5, 2.5 }
        table.sort(t)
        test.expect(t[1]).toBe(1.5)
        test.expect(t[3]).toBe(3.5)
        table.sort(t, function(a, b) return a > b end)
        test.expect(t[1]).toBe(3.5)
        test.expect(t[3]).toBe(1.5)
    end)

    test.it("table.concat joins float elements", function()
        local t = { 0.5, 1.5 }
        local s = table.concat(t, ",")
        test.expect(s).toBe("0.5,1.5")
    end)

    test.it("table.unpack returns the elements", function()
        local t = { 0.5, 1.5, 2.5 }
        local a, b, c = table.unpack(t)
        test.expect(a).toBe(0.5)
        test.expect(b).toBe(1.5)
        test.expect(c).toBe(2.5)
    end)

    test.it("table.maxn sees the full float array", function()
        local t = { 0.5, 1.5, 2.5 }
        test.expect(table.maxn(t)).toBe(3)
    end)
end)

test.describe("float arrays: delegation", function()
    test.it("reads through a Forward chain reach a float array", function()
        local parent = { 0.5, 1.5 }
        local child = {}
        table.forward(child, parent)
        test.expect(child[2]).toBe(1.5)
        test.expect(child[3]).toBeNil()
    end)

    test.it("misses in a float array continue to the Forward chain",
            function()
        local parent = {}
        parent.deep = "found"
        local child = { 0.5 }
        table.forward(child, parent)
        test.expect(child[1]).toBe(0.5)
        test.expect(child.deep).toBe("found")
    end)
end)

assert(test.run())
