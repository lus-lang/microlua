--[[
  MicroLua parse-time integer constant folding tests
  /tests/interpreter/test_fold.lua

  Every folded expression is compared against the same computation routed
  through opaque locals (which cannot fold), so the parse-time arithmetic
  must agree with the VM's runtime arithmetic bit-for-bit.
]]

local test = require("_base")

-- Launder a value through a local the parser cannot see through.
local function opaque(v)
    local t = { v }
    return t[1]
end

test.describe("integer folding agrees with runtime", function()
    test.it("folds simple arithmetic", function()
        local a, b, c = opaque(60), opaque(60), opaque(24)
        test.expect(60 * 60 * 24).toBe(a * b * c)
        test.expect(1 + 2).toBe(opaque(1) + opaque(2))
        test.expect(10 - 3).toBe(opaque(10) - opaque(3))
        test.expect(7 * -8).toBe(opaque(7) * opaque(-8))
        test.expect(100 - 250).toBe(opaque(100) - opaque(250))
    end)

    test.it("folds negative literals", function()
        test.expect(-5).toBe(opaque(5) - 10 + 5 - 5)
        test.expect(-128).toBe(opaque(-128))
        test.expect(-(3 + 4)).toBe(opaque(-7))
        test.expect(- -9).toBe(opaque(9))
    end)

    test.it("wraps exactly like the runtime", function()
        local big = opaque(2147483647)
        test.expect(2147483647 + 1).toBe(big + 1)
        test.expect(2147483647 + 2147483647).toBe(big + big)
        local negbig = opaque(-2147483647 - 1)
        test.expect(-2147483647 - 1).toBe(negbig)
        test.expect(-2147483647 - 2).toBe(negbig - 1)
        test.expect(65536 * 65536).toBe(opaque(65536) * opaque(65536))
        test.expect(123456789 * 987654321)
            .toBe(opaque(123456789) * opaque(987654321))
    end)

    test.it("folds non-negative modulo only where semantics are exact", function()
        test.expect(17 % 5).toBe(opaque(17) % opaque(5))
        test.expect(0 % 7).toBe(opaque(0) % opaque(7))
        test.expect(100 % 100).toBe(opaque(100) % opaque(100))
        -- Negative operands are deliberately NOT folded, but must still
        -- match the runtime result (both go through the VM).
        test.expect(-17 % 5).toBe(opaque(-17) % opaque(5))
        test.expect(17 % -5).toBe(opaque(17) % opaque(-5))
    end)

    test.it("mixed literal/variable expressions still work", function()
        local x = opaque(10)
        test.expect(x + 2 * 3).toBe(16)   -- 2*3 folds, x+6 does not
        test.expect(2 * 3 + x).toBe(16)
        test.expect(x * (5 - 3)).toBe(20)
        test.expect((1 + 2) * (3 + 4)).toBe(21)
    end)

    test.it("does not fold float or division expressions", function()
        -- These evaluate at runtime either way; equality just confirms
        -- nothing broke around the non-folding paths.
        test.expect(7 / 2).toBe(opaque(7) / opaque(2))
        test.expect(2 ^ 10).toBe(opaque(2) ^ opaque(10))
        test.expect(1.5 + 2.5).toBe(opaque(1.5) + opaque(2.5))
        test.expect(1 + 2.5).toBe(opaque(1) + opaque(2.5))
    end)

    test.it("folds inside larger expressions and statements", function()
        local t = { 0, 0, 0, 0 }
        t[2 + 3] = 60 * 60
        test.expect(t[5]).toBe(3600)
        local n = 0
        for i = 1, 2 + 2 do n = n + 1 end
        test.expect(n).toBe(4)
        test.expect(string.rep("x", 2 * 3)).toBe("xxxxxx")
    end)
end)

assert(test.run())
