--[[
  MicroLua String Library Tests
  /tests/interpreter/test_string.lua
]]

local test = require("_base")

test.describe("string.format", function()
    test.it("formats strings with %s", function()
        local result = string.format("hello %s", "world")
        test.expect(result).toBe("hello world")
    end)

    test.it("formats integers with %d", function()
        local result = string.format("x = %d", 42)
        test.expect(result).toBe("x = 42")
    end)

    test.it("formats multiple values", function()
        local result = string.format("%s = %d", "answer", 42)
        test.expect(result).toBe("answer = 42")
    end)
end)

test.describe("string.find", function()
    test.it("finds plain substring", function()
        local a, b = string.find("hello world", "world", 1, true)
        test.expect(a).toBe(7)
        test.expect(b).toBe(11)
    end)

    test.it("finds pattern with %d+", function()
        local a, b = string.find("test123", "%d+")
        test.expect(a).toBe(5)
        test.expect(b).toBe(7)
    end)

    test.it("returns nil when not found", function()
        local a = string.find("hello", "xyz")
        test.expect(a).toBeNil()
    end)
end)

test.describe("string.match", function()
    test.it("matches digit pattern", function()
        local result = string.match("test123", "%d+")
        test.expect(result).toBe("123")
    end)

    test.it("matches word pattern", function()
        local result = string.match("hello world", "%w+")
        test.expect(result).toBe("hello")
    end)

    test.it("returns nil when no match", function()
        local result = string.match("hello", "%d+")
        test.expect(result).toBeNil()
    end)
end)

test.describe("string.pack/unpack", function()
    test.it("packs byte correctly", function()
        local packed = string.pack("B", 255)
        test.expect(#packed).toBe(1)
    end)

    test.it("computes packsize for fixed formats", function()
        local size = string.packsize("bHI")
        test.expect(size).toBe(7) -- 1 + 2 + 4
    end)
end)

test.describe("string.sub", function()
    test.it("extracts substring", function()
        local result = string.sub("hello", 2, 4)
        test.expect(result).toBe("ell")
    end)

    test.it("handles negative indices", function()
        local result = string.sub("hello", -2)
        test.expect(result).toBe("lo")
    end)
end)

test.describe("string.upper/lower", function()
    test.it("converts to uppercase", function()
        test.expect(string.upper("hello")).toBe("HELLO")
    end)

    test.it("converts to lowercase", function()
        test.expect(string.lower("HELLO")).toBe("hello")
    end)
end)

test.describe("string.rep", function()
    test.it("repeats string", function()
        test.expect(string.rep("ab", 3)).toBe("ababab")
    end)

    test.it("handles zero repetitions", function()
        test.expect(string.rep("x", 0)).toBe("")
    end)
end)

test.describe("string.reverse", function()
    test.it("reverses string", function()
        test.expect(string.reverse("hello")).toBe("olleh")
    end)
end)

test.run()
