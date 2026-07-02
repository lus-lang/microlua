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

test.describe("multiple assignment", function()
    test.it("swaps locals", function()
        local a, b = 1, 2
        a, b = b, a
        test.expect(a).toBe(2)
        test.expect(b).toBe(1)
    end)

    test.it("assigns from a multi-result call", function()
        local function two() return 10, 20 end
        local a, b
        a, b = two()
        test.expect(a).toBe(10)
        test.expect(b).toBe(20)
    end)

    test.it("nil-pads missing values", function()
        local a, b, c
        a, b, c = 1
        test.expect(a).toBe(1)
        test.expect(b).toBeNil()
        test.expect(c).toBeNil()
    end)

    test.it("assigns table fields and indices", function()
        local t = {}
        local u = {}
        t.x, u[1], t.y = 5, 6, 7
        test.expect(t.x).toBe(5)
        test.expect(u[1]).toBe(6)
        test.expect(t.y).toBe(7)
    end)

    test.it("mixes locals, globals and fields", function()
        local t = {}
        local loc
        MULTI_G, loc, t.f = "g", "l", "f"
        test.expect(MULTI_G).toBe("g")
        test.expect(loc).toBe("l")
        test.expect(t.f).toBe("f")
        MULTI_G = nil
    end)
end)

test.describe("lexical scopes", function()
    test.it("does not leak do-block locals", function()
        do
            local scoped = 1
            test.expect(scoped).toBe(1)
        end
        test.expect(scoped).toBeNil()
    end)

    test.it("does not leak branch locals", function()
        if true then
            local branch = 2
            test.expect(branch).toBe(2)
        else
            local branch = 3
            test.expect(branch).toBe(3)
        end
        test.expect(branch).toBeNil()
    end)

    test.it("does not leak loop-body locals", function()
        local n = 0
        while n < 1 do
            local body = 4
            test.expect(body).toBe(4)
            n = n + 1
        end
        repeat
            local repeated = 5
            test.expect(repeated).toBe(5)
            n = n + 1
        until n == 2
        for i = 1, 1 do
            local numeric = i
            test.expect(numeric).toBe(1)
        end
        for _, v in ipairs({ 6 }) do
            local generic = v
            test.expect(generic).toBe(6)
        end
        test.expect(body).toBeNil()
        test.expect(repeated).toBeNil()
        test.expect(numeric).toBeNil()
        test.expect(generic).toBeNil()
    end)

    test.it("cleans locals on break without breaking captures", function()
        local saved
        while true do
            do
                local captured = 7
                saved = function() return captured end
                break
            end
        end
        test.expect(saved()).toBe(7)
        test.expect(captured).toBeNil()
    end)
end)

test.describe("break inside conditionals", function()
    -- Regression: an if used to capture break jumps pushed inside its arms
    -- and retarget them to the end of the if instead of the loop exit.
    test.it("breaks a while-true loop from inside an if", function()
        local n = 0
        while true do
            if n > 3 then
                break
            end
            n = n + 1
        end
        test.expect(n).toBe(4)
    end)

    test.it("breaks a conditioned while loop early", function()
        local n = 0
        while n < 200 do
            if n > 100 then
                break
            end
            n = n + 1
        end
        test.expect(n).toBe(101)
    end)

    test.it("breaks a numeric for from inside an if", function()
        local last = 0
        for i = 1, 100 do
            if i > 3 then
                break
            end
            last = i
        end
        test.expect(last).toBe(3)
    end)

    test.it("breaks from an elseif arm", function()
        local s = ""
        for i = 1, 5 do
            if i == 2 then
                s = s .. "x"
            elseif i == 4 then
                break
            else
                s = s .. "y"
            end
        end
        test.expect(s).toBe("yxy")
    end)

    test.it("breaks nested loops independently", function()
        local n = 0
        for i = 1, 10 do
            if i == 3 then
                break
            end
            for j = 1, 10 do
                if j == 2 then
                    break
                end
                n = n + 1
            end
        end
        test.expect(n).toBe(2)
    end)

    test.it("breaks a repeat loop from inside an if", function()
        local n = 0
        repeat
            if n > 3 then
                break
            end
            n = n + 1
        until false
        test.expect(n).toBe(4)
    end)

    test.it("breaks a generic for from inside an if", function()
        local t = { 10, 20, 30, 40 }
        local sum = 0
        for _, v in ipairs(t) do
            if v > 20 then
                break
            end
            sum = sum + v
        end
        test.expect(sum).toBe(30)
    end)
end)

assert(test.run())
