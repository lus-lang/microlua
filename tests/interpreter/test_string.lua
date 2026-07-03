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

    test.it("matches patterns by byte by design", function()
        local m = string.match("éa", ".")
        test.expect(#m).toBe(1)
        local a, b = string.find("éa", "a")
        test.expect(a).toBe(3)
        test.expect(b).toBe(3)
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

    test.it("round-trips 4-byte values with high bytes set", function()
        -- Bytes >= 0x80 in the upper positions exercise the <<16/<<24
        -- reassembly paths, which must not depend on the width of int.
        local function rt(fmt, v)
            local value = string.unpack(fmt, string.pack(fmt, v))
            return value
        end
        test.expect(rt("I", 0x12345678)).toBe(305419896)
        test.expect(rt("I", 8388608)).toBe(8388608)     -- 00 00 80 00
        test.expect(rt("I", -2147483648)).toBe(-2147483648) -- 00 00 00 80
        test.expect(rt("I", 2147483647)).toBe(2147483647)   -- FF FF FF 7F
        test.expect(rt("i", -1)).toBe(-1)               -- FF FF FF FF
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

test.describe("unicode awareness", function()
    test.it("len counts codepoints", function()
        test.expect(string.len("héllo")).toBe(5)
        test.expect(string.len("日本語")).toBe(3)
        test.expect(string.len("")).toBe(0)
    end)

    test.it("sub uses codepoint indices", function()
        test.expect(string.sub("日本語テスト", 2, 4)).toBe("本語テ")
        test.expect(string.sub("héllo", 2, 2)).toBe("é")
        test.expect(string.sub("日本語", -2)).toBe("本語")
    end)

    test.it("byte returns codepoints", function()
        test.expect(string.byte("日")).toBe(26085)
        test.expect(string.byte("A")).toBe(65)
        local a, b = string.byte("héllo", 1, 2)
        test.expect(a).toBe(104)
        test.expect(b).toBe(233)
    end)

    test.it("char encodes codepoints as UTF-8", function()
        test.expect(string.char(72, 105)).toBe("Hi")
        test.expect(string.char(0x65E5)).toBe("日")
        test.expect(string.len(string.char(0x65E5, 65))).toBe(2)
    end)

    test.it("reverse keeps multibyte sequences intact", function()
        test.expect(string.reverse("日本語")).toBe("語本日")
        test.expect(string.reverse("aé")).toBe("éa")
    end)

    -- Positional ops take an O(1) path on strings whose cached all-ASCII
    -- flag is set. These pin down that the flag (a) speeds ASCII strings
    -- without changing results, (b) is NOT set when any byte is multibyte,
    -- across every way a string acquires its header: literal/interning,
    -- concat folding, and library builders.
    test.it("positional ops on long ascii strings", function()
        local s = "abcdefghijklmnop" -- long: heap-interned, flagged
        test.expect(string.len(s)).toBe(16)
        test.expect(string.byte(s, 16)).toBe(112)
        test.expect(string.byte(s, -1)).toBe(112)
        test.expect(string.sub(s, 3, 5)).toBe("cde")
        test.expect(string.sub(s, -3)).toBe("nop")
        test.expect(string.sub(s, 5, 100)).toBe("efghijklmnop")
        test.expect(string.sub(s, 9, 3)).toBe("")
        local a, b, c = string.byte(s, 2, 4)
        test.expect(a).toBe(98)
        test.expect(b).toBe(99)
        test.expect(c).toBe(100)
    end)

    test.it("concat-built ascii strings stay codepoint-correct", function()
        local s = "start"
        for i = 1, 5 do
            s = s .. "-seg" .. i
        end
        test.expect(string.len(s)).toBe(30)
        test.expect(string.sub(s, 6, 10)).toBe("-seg1")
        test.expect(string.byte(s, 30)).toBe(53) -- '5'
    end)

    test.it("concat with multibyte tail is not treated as ascii", function()
        local s = "abcdefgh" .. "é" -- ascii head, multibyte fold
        test.expect(string.len(s)).toBe(9)
        test.expect(string.byte(s, 9)).toBe(233)
        test.expect(string.sub(s, 9, 9)).toBe("é")
        test.expect(string.sub(s, -1)).toBe("é")
    end)

    test.it("concat with multibyte head is not treated as ascii", function()
        local s = "é" .. "abcdefgh"
        test.expect(string.len(s)).toBe(9)
        test.expect(string.byte(s, 1)).toBe(233)
        test.expect(string.sub(s, 2, 4)).toBe("abc")
    end)

    test.it("multibyte deep inside a long string", function()
        local s = "aaaaaaaaaa日bbbbbbbbbb"
        test.expect(string.len(s)).toBe(21)
        test.expect(string.byte(s, 11)).toBe(26085)
        test.expect(string.sub(s, 11, 11)).toBe("日")
        test.expect(string.sub(s, 12, 14)).toBe("bbb")
        test.expect(string.sub(s, -10)).toBe("bbbbbbbbbb")
    end)

    test.it("library-built long strings scan correctly", function()
        local r = string.rep("AB", 500)
        test.expect(string.len(r)).toBe(1000)
        local n = 0
        for k = 1, 1000 do
            if string.byte(r, k) == 65 then
                n = n + 1
            end
        end
        test.expect(n).toBe(500)
        test.expect(string.sub(r, 999, 1000)).toBe("AB")
    end)

    test.it("upper and lower are ASCII-only by design", function()
        test.expect(string.upper("héllo")).toBe("HéLLO")
        test.expect(string.lower("HÉLLO")).toBe("hÉllo")
    end)

    test.it("dumps functions larger than the old fixed buffer", function()
        local big = string.rep("x", 20000)
        local f = load("return '" .. big .. "'")
        local dumped = string.dump(f)
        local a, b, c, d = string.byte(dumped, 1, 4)
        test.expect(a).toBe(27)
        test.expect(b).toBe(77)
        test.expect(c).toBe(76)
        test.expect(d).toBe(117)
        test.expect(#dumped > 20000).toBe(true)
    end)

    test.it("case mapping handles long strings", function()
        local big = string.rep("ab", 800) -- 1600 bytes > the old 1024 cap
        local up = string.upper(big)
        test.expect(string.len(up)).toBe(1600)
        test.expect(string.sub(up, 1, 2)).toBe("AB")
        test.expect(string.sub(up, -2)).toBe("AB")
    end)
end)

test.describe("concatenation", function()
    test.it("joins two strings", function()
        test.expect("foo" .. "bar").toBe("foobar")
    end)

    test.it("chains many operands (all-string fast path)", function()
        test.expect("a" .. "bb" .. "ccc" .. "dddd").toBe("abbcccdddd")
        local a, b, c = "hello", " ", "world"
        test.expect(a .. b .. c).toBe("hello world")
    end)

    test.it("handles empty operands", function()
        test.expect("" .. "x").toBe("x")
        test.expect("x" .. "").toBe("x")
        test.expect("" .. "").toBe("")
    end)

    test.it("produces short (<=3 byte) results correctly", function()
        test.expect("a" .. "b").toBe("ab")
        test.expect("a" .. "b" .. "c").toBe("abc")
    end)

    test.it("interns equal results to the same value", function()
        local x = "ab" .. "cdef"
        local y = "abc" .. "def"
        test.expect(x).toBe(y)          -- equal contents
        test.expect(x == y).toBeTrue()  -- and identical (interned)
    end)

    test.it("converts numbers in concatenation (number path)", function()
        test.expect("n=" .. 42).toBe("n=42")
        test.expect(1 .. 2 .. 3).toBe("123")
        test.expect("x" .. 1 .. "y" .. 2).toBe("x1y2")
        test.expect("neg" .. -7).toBe("neg-7")
    end)

    test.it("builds long strings in a loop", function()
        local s = ""
        for i = 1, 50 do s = s .. "ab" end
        test.expect(#s).toBe(100)
        test.expect(string.sub(s, 1, 4)).toBe("abab")
        test.expect(string.sub(s, -2)).toBe("ab")
    end)
end)

test.describe("concat renders every numeric type", function()
    test.it("floats concatenate instead of vanishing", function()
        -- Regression: the .. operator dropped float operands outright.
        test.expect("x" .. 1.5).toBe("x1.5")
        test.expect(1.25 .. "z").toBe("1.25z")
        test.expect("a" .. 0.5 .. "b").toBe("a0.5b")
    end)

    test.it("integer extremes concatenate exactly", function()
        -- Regression: INT_MIN's negation overflowed and emitted garbage.
        test.expect("m" .. (-2147483647 - 1)).toBe("m-2147483648")
        test.expect("p" .. 2147483647).toBe("p2147483647")
        test.expect("z" .. 0).toBe("z0")
        test.expect("n" .. -1).toBe("n-1")
    end)

    test.it("mixed chains agree with tostring", function()
        local v = 7.25
        test.expect("v=" .. v).toBe("v=" .. tostring(v))
        local i = -42
        test.expect("i=" .. i).toBe("i=" .. tostring(i))
    end)
end)

assert(test.run())
