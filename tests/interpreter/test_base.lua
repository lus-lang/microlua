--[[
  MicroLua Base Library Tests
  /tests/interpreter/test_base.lua
]]

local test = require("_base")

test.describe("tonumber", function()
    test.it("converts decimal string", function()
        test.expect(tonumber("42")).toBe(42)
    end)

    test.it("converts hex string", function()
        test.expect(tonumber("0xFF")).toBe(255)
    end)

    test.it("converts with base 2", function()
        test.expect(tonumber("1010", 2)).toBe(10)
    end)

    test.it("converts with base 16", function()
        test.expect(tonumber("FF", 16)).toBe(255)
    end)

    test.it("returns nil for invalid", function()
        test.expect(tonumber("abc")).toBeNil()
    end)
end)

test.describe("tostring", function()
    test.it("converts number", function()
        test.expect(tostring(42)).toBe("42")
    end)

    test.it("converts boolean true", function()
        test.expect(tostring(true)).toBe("true")
    end)

    test.it("converts boolean false", function()
        test.expect(tostring(false)).toBe("false")
    end)

    test.it("converts nil", function()
        test.expect(tostring(nil)).toBe("nil")
    end)
end)

test.describe("type", function()
    test.it("returns 'number' for numbers", function()
        test.expect(type(42)).toBe("number")
        test.expect(type(3.14)).toBe("number")
    end)

    test.it("returns 'string' for strings", function()
        test.expect(type("hello")).toBe("string")
    end)

    test.it("returns 'boolean' for booleans", function()
        test.expect(type(true)).toBe("boolean")
        test.expect(type(false)).toBe("boolean")
    end)

    test.it("returns 'nil' for nil", function()
        test.expect(type(nil)).toBe("nil")
    end)

    test.it("returns 'table' for tables", function()
        test.expect(type({})).toBe("table")
    end)

    test.it("returns 'function' for functions", function()
        test.expect(type(print)).toBe("function")
    end)
end)

test.describe("assert", function()
    test.it("returns value when truthy", function()
        test.expect(assert(42)).toBe(42)
        test.expect(assert("hello")).toBe("hello")
    end)
end)

test.describe("select", function()
    test.it("returns count with '#'", function()
        local function f(...)
            return select("#", ...)
        end
        test.expect(f(1, 2, 3)).toBe(3)
    end)
end)

test.describe("pairs/ipairs", function()
    test.it("ipairs iterates array", function()
        local sum = 0
        for i, v in ipairs({ 10, 20, 30 }) do
            sum = sum + v
        end
        test.expect(sum).toBe(60)
    end)

    test.it("pairs iterates table", function()
        local count = 0
        for k, v in pairs({ a = 1, b = 2 }) do
            count = count + 1
        end
        test.expect(count).toBe(2)
    end)
end)

test.run()
