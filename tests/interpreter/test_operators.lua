--[[
  MicroLua Operator Precedence & Associativity Tests
  /tests/interpreter/test_operators.lua

  Regression coverage for the Pratt-parser climbing bug where left-associative
  operators recursed at (prec + 1), making `2 + 3 * 4` parse as `(2 + 3) * 4`.
  Comparisons use `==` so exponentiation's float result doesn't perturb the
  check (we test the value, not its formatting).
]]

local test = require("_base")

test.describe("arithmetic precedence", function()
    test.it("multiplication binds tighter than addition", function()
        test.expect(2 + 3 * 4).toBe(14)   -- the exact bug: NOT (2+3)*4 == 20
        test.expect(1 + 1 * 5).toBe(6)
        test.expect(2 * 3 + 1).toBe(7)
    end)

    test.it("multiplication binds tighter than subtraction", function()
        test.expect(8 - 2 * 3).toBe(2)
        test.expect(100 - 5 * 3 + 2).toBe(87)
    end)

    test.it("modulo binds tighter than addition", function()
        test.expect(1 + 2 % 3).toBe(3)        -- 1 + (2%3)
        test.expect(10 + 9 % 4).toBe(11)      -- 10 + (9%4)
    end)
end)

test.describe("associativity", function()
    test.it("addition/subtraction are left-associative", function()
        test.expect(2 - 3 + 4).toBe(3)        -- (2-3)+4, not 2-(3+4)
        test.expect(100 - 10 - 1).toBe(89)    -- (100-10)-1, not 100-(10-1)
    end)

    test.it("multiplication is left-associative", function()
        test.expect(2 * 3 * 4).toBe(24)
    end)

    test.it("exponentiation is right-associative", function()
        test.expect(2 ^ 3 ^ 2 == 512).toBeTrue()   -- 2^(3^2)=2^9, not (2^3)^2=64
        test.expect(2 ^ 2 ^ 3 == 256).toBeTrue()   -- 2^(2^3)=2^8, not (2^2)^3=64
    end)

    test.it("unary minus binds looser than exponentiation", function()
        test.expect(-2 ^ 2 == -4).toBeTrue()       -- -(2^2), not (-2)^2
    end)
end)

test.describe("cross-level precedence", function()
    test.it("arithmetic binds tighter than comparison", function()
        test.expect(1 + 2 < 3 + 4).toBeTrue()      -- (1+2) < (3+4)
        test.expect(2 * 3 == 6).toBeTrue()
        test.expect(2 + 2 * 3 - 1).toBe(7)
    end)

    test.it("comparison binds tighter than and/or", function()
        test.expect(2 + 3 == 5 and 1 or 0).toBe(1)
        test.expect(1 + 1 == 2 and 3 * 2 == 6).toBeTrue()
    end)

    test.it("and binds tighter than or", function()
        -- false and X is false, so result is the `or` branch
        test.expect(false and true or true).toBeTrue()
        test.expect(true or false and false).toBeTrue()
    end)

    test.it("not binds tighter than comparison", function()
        test.expect(not 1 == 2).toBeFalse()        -- (not 1) == 2 -> false == 2
    end)
end)

test.describe("concatenation precedence", function()
    test.it("addition binds tighter than concatenation", function()
        test.expect(1 .. 2 + 3).toBe("15")         -- 1 .. (2+3)
    end)

    test.it("concatenation chains", function()
        test.expect(1 .. 2 .. 3).toBe("123")
    end)
end)

test.describe("compare-branch fusion", function()
    test.it("statement conditions behave across all four comparisons", function()
        local hits = 0
        for i = 1, 10 do
            if i < 3 then hits = hits + 1 end
            if i <= 3 then hits = hits + 10 end
            if i == 3 then hits = hits + 100 end
            if i ~= 3 then hits = hits + 1000 end
        end
        test.expect(hits).toBe(2 + 30 + 100 + 9000)
        local i = 0
        while i < 7 do i = i + 2 end
        test.expect(i).toBe(8)
        local j = 10
        repeat j = j - 3 until j <= 0
        test.expect(j).toBe(-2)
    end)

    test.it("comparison results used as values still work", function()
        local b = 1 < 2
        test.expect(b).toBe(true)
        test.expect(type(3 == 3)).toBe("boolean")
        local c = (1 < 2) and (4 < 3)
        test.expect(c).toBe(false)
        test.expect(not (5 < 1)).toBe(true)
        local function pred(x) return x <= 7 end
        test.expect(pred(7)).toBe(true)
        test.expect(pred(8)).toBe(false)
    end)

    test.it("and-chain jumps land after a fused condition", function()
        -- Regression: `a and x < y` patches the and-jump to the position
        -- right after the compare; fusing the compare into the branch
        -- must not strand that target inside the fused instruction.
        local t = { 3, 1, 2 }
        local seen = 0
        for i = 1, 3 do
            if i > 1 and t[i - 1] > t[i] then
                seen = seen + 1
            end
        end
        test.expect(seen).toBe(1)
        local flag = false
        if flag and 1 < 2 then seen = -1 end
        test.expect(seen).toBe(1)
        if (2 < 3) and (4 < 5) then seen = seen + 10 end
        test.expect(seen).toBe(11)
    end)

    test.it("locals read in fused-condition loops are not cleared early", function()
        -- Regression guard for the last-use optimizer: a fused backward
        -- branch (repeat/until compare) is still a loop back-edge, so a
        -- body-local read inside the loop is NOT a last use.
        local x = 5
        local acc = 0
        local i = 0
        repeat
            acc = acc + x -- only textual read of x; loops 3 times
            i = i + 1
        until i >= 3
        test.expect(acc).toBe(15)
    end)
end)

assert(test.run())
