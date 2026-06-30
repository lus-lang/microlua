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

assert(test.run())
