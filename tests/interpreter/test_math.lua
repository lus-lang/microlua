--[[
  MicroLua Math Library Tests
  /tests/interpreter/test_math.lua
]]

local test = require("_base")

test.describe("basics", function()
    test.it("abs/floor/ceil", function()
        test.expect(math.abs(-5)).toBe(5)
        test.expect(math.floor(3.7)).toBe(3)
        test.expect(math.ceil(3.2)).toBe(4)
    end)

    test.it("min/max", function()
        test.expect(math.max(1, 9, 4)).toBe(9)
        test.expect(math.min(1, 9, 4)).toBe(1)
    end)

    test.it("sqrt and pow", function()
        test.expect(math.sqrt(81)).toBe(9)
        test.expect(math.pow(2, 10)).toBe(1024)
        test.expect(math.pow(9, 0.5)).toBe(3)
    end)

    test.it("fmod", function()
        test.expect(math.fmod(7, 3)).toBe(1)
    end)

    test.it("constants", function()
        test.expect(math.pi > 3.14159 and math.pi < 3.1416).toBe(true)
        test.expect(math.huge > 1e308).toBe(true)
    end)
end)

test.describe("Lua 5.3 additions", function()
    test.it("math.type distinguishes int and float", function()
        test.expect(math.type(1)).toBe("integer")
        test.expect(math.type(1.5)).toBe("float")
        test.expect(math.type("x")).toBeNil()
    end)

    test.it("math.tointeger converts representable floats", function()
        test.expect(math.tointeger(5.0)).toBe(5)
        test.expect(math.tointeger(5.5)).toBeNil()
    end)

    test.it("maxinteger/mininteger exist", function()
        test.expect(math.maxinteger > 0).toBe(true)
        test.expect(math.mininteger < 0).toBe(true)
    end)

    test.it("math.ult compares unsigned", function()
        test.expect(math.ult(1, 2)).toBe(true)
        test.expect(math.ult(-1, 1)).toBe(false) -- -1 is huge unsigned
    end)
end)

test.describe("random", function()
    test.it("stays within range", function()
        math.randomseed(42)
        for _ = 1, 20 do
            local r = math.random(1, 6)
            test.expect(r >= 1 and r <= 6).toBe(true)
        end
    end)
end)

assert(test.run())
