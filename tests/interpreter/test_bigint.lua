--[[
  MicroLua Boxed-Integer Tests
  /tests/interpreter/test_bigint.lua

  Integers outside the 29-bit inline range are heap-boxed on the 32-bit tagging
  path and stored inline on the 64-bit NaN-boxing path. Every assertion here must
  hold identically on both. The inline boundary is 2^28 = 268435456, so the
  values below (hundreds of millions) exercise the boxing path on 32-bit targets.
]]

local test = require("_base")

test.describe("boxed integers", function()
    test.it("literals above the inline range round-trip", function()
        local n = 300000000
        test.expect(n).toBe(300000000)
        test.expect(-n).toBe(-300000000)
        test.expect(n > 0).toBe(true)
        test.expect(-n < 0).toBe(true)
    end)

    test.it("arithmetic that crosses the inline boundary", function()
        local a = 200000000
        local b = 200000000
        test.expect(a + b).toBe(400000000)      -- result boxed on 32-bit
        test.expect(500000000 - 1).toBe(499999999)
        test.expect(200000000 * 10).toBe(2000000000)
        test.expect(2000000000 / 1000).toBe(2000000)  -- exact division stays integer
        test.expect(-300000000 + 100000000).toBe(-200000000)
        test.expect(600000000 % 7).toBe(5)
    end)

    test.it("equality and comparison across representations", function()
        test.expect(300000000 == 100000000 + 200000000).toBe(true)
        test.expect(500000000 > 400000000).toBe(true)
        test.expect(-500000000 < -400000000).toBe(true)
        test.expect(300000000 ~= 300000001).toBe(true)
        test.expect(300000000 == 300000000.0).toBe(true) -- boxed int vs float
    end)

    test.it("boxed values as (hash-part) table keys", function()
        -- Positive integer keys beyond len+1 are array holes (a runtime error by
        -- design on every target), so exercise boxed keys via the hash part with
        -- negative big integers.
        local t = {}
        local k = -400000000
        t[k] = "x"
        test.expect(t[k]).toBe("x")
        t[-500000000] = 5
        t[-600000000] = 6
        test.expect(t[-500000000]).toBe(5)
        test.expect(t[-600000000]).toBe(6)
        -- a computed key must find the entry stored under a literal key
        test.expect(t[-100000000 - 400000000]).toBe(5)
        t[k] = "y"
        test.expect(t[k]).toBe("y")            -- update, not duplicate
    end)

    test.it("math.type / type / tostring", function()
        test.expect(math.type(300000000)).toBe("integer")
        test.expect(type(300000000)).toBe("number")
        test.expect(tostring(300000000)).toBe("300000000")
        test.expect(tostring(-300000000)).toBe("-300000000")
        test.expect(type(1.5)).toBe("number")  -- heap-float classification fix
    end)

    test.it("string.format on boxed integers", function()
        test.expect(string.format("%d", 300000000)).toBe("300000000")
        test.expect(string.format("%d", -300000000)).toBe("-300000000")
        test.expect(string.format("%x", 305419896)).toBe("12345678")
    end)

    test.it("numeric for-loop crossing the boundary", function()
        local count = 0
        local last = 0
        for i = 268435454, 268435458 do        -- spans 2^28 (inline -> boxed)
            count = count + 1
            last = i
        end
        test.expect(count).toBe(5)
        test.expect(last).toBe(268435458)
    end)

    test.it("math.maxinteger/mininteger are full 32-bit", function()
        test.expect(math.maxinteger).toBe(2147483647)
        test.expect(math.type(math.maxinteger)).toBe("integer")
        test.expect(math.maxinteger > 0).toBe(true)
        test.expect(math.mininteger < 0).toBe(true)
    end)

    test.it("math.floor / tointeger produce boxed integers", function()
        test.expect(math.floor(300000000.5)).toBe(300000000)
        test.expect(math.type(math.floor(300000000.5))).toBe("integer")
    end)

    test.it("heap floats stringify (32-bit path)", function()
        -- Exact in both binary64 and binary32, so this holds on every build,
        -- including -DMLUA_FLOAT=float. Guards the 32-bit heap-float tostring.
        test.expect(tostring(0.75)).toBe("0.75")
        test.expect(tostring(2.5)).toBe("2.5")
        test.expect(tostring(-0.5)).toBe("-0.5")
        test.expect(math.type(0.75)).toBe("float")
    end)
end)

assert(test.run())
