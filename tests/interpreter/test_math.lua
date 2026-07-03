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

    test.it("Lua 5.1 compatibility aliases and functions", function()
        test.expect(math.mod(7, 3)).toBe(1)
        test.expect(math.atan2(0, 1)).toBe(0)
        test.expect(math.log10(1000)).toBe(3)
        test.expect(math.cosh(0)).toBe(1)
        test.expect(math.sinh(0)).toBe(0)
        test.expect(math.tanh(0)).toBe(0)
    end)

    test.it("constants", function()
        test.expect(math.pi > 3.14159 and math.pi < 3.1416).toBe(true)
        test.expect(math.huge > 1e308).toBe(true)
    end)
end)

test.describe("Lua 5.3 additions", function()
    test.it("math.type distinguishes int and float", function()
        test.expect(math.type(1)).toBe("integer")
        test.expect(math.type(1.0)).toBe("float")
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

test.describe("unary function table", function()
    -- Every entry routed through the shared Math1 body: pin one exact or
    -- near-exact value each so a mis-wired thunk (wrong function for a
    -- name) cannot pass.
    local function near(a, b)
        local d = a - b
        if d < 0 then d = -d end
        return d < 1e-5
    end

    test.it("dispatches each name to the right function", function()
        test.expect(math.abs(-3)).toBe(3)
        test.expect(math.floor(2.7)).toBe(2)
        test.expect(math.ceil(2.2)).toBe(3)
        test.expect(math.sqrt(9)).toBe(3)
        test.expect(near(math.sin(0), 0)).toBeTrue()
        test.expect(near(math.cos(0), 1)).toBeTrue()
        test.expect(near(math.tan(0), 0)).toBeTrue()
        test.expect(near(math.asin(1), math.pi / 2)).toBeTrue()
        test.expect(near(math.acos(1), 0)).toBeTrue()
        test.expect(near(math.exp(0), 1)).toBeTrue()
        test.expect(near(math.exp(1), 2.71828)).toBeTrue()
        test.expect(near(math.log10(100), 2)).toBeTrue()
        test.expect(near(math.deg(math.pi), 180)).toBeTrue()
        test.expect(near(math.rad(180), math.pi)).toBeTrue()
        test.expect(near(math.cosh(0), 1)).toBeTrue()
        test.expect(near(math.sinh(0), 0)).toBeTrue()
        test.expect(near(math.tanh(0), 0)).toBeTrue()
    end)

    test.it("distinguishes easily-confused neighbors", function()
        -- sin vs sinh, cos vs cosh, tan vs tanh at x=1 differ clearly
        test.expect(near(math.sin(1), 0.84147)).toBeTrue()
        test.expect(near(math.sinh(1), 1.17520)).toBeTrue()
        test.expect(near(math.cos(1), 0.54030)).toBeTrue()
        test.expect(near(math.cosh(1), 1.54308)).toBeTrue()
        test.expect(near(math.tan(1), 1.55740)).toBeTrue()
        test.expect(near(math.tanh(1), 0.76159)).toBeTrue()
    end)
end)

assert(test.run())
