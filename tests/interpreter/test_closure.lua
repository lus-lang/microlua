--[[
  MicroLua Closure & Upvalue Tests
  /tests/interpreter/test_closure.lua
]]

-- Framework-independent smoke checks first: if closures regress, the test
-- framework itself (which is closure-based) cannot be trusted to report it.
do
    local x = 5
    local f = function() return x end
    assert(f() == 5, "smoke: read capture")

    local function mk(v) return function() return v end end
    assert(mk(7)() == 7, "smoke: param capture")

    local n = 0
    local function inc() n = n + 1 return n end
    inc()
    assert(inc() == 2, "smoke: upvalue write")
end

local test = require("_base")

test.describe("closure basics", function()
    test.it("captures a local for reading", function()
        local x = 42
        local f = function() return x end
        test.expect(f()).toBe(42)
    end)

    test.it("captures parameters", function()
        local function mk(v) return function() return v end end
        test.expect(mk(7)()).toBe(7)
        test.expect(mk(9)()).toBe(9)
    end)

    test.it("keeps factory instances independent", function()
        local function mk(v) return function() return v end end
        local a, b = mk(1), mk(2)
        test.expect(a()).toBe(1)
        test.expect(b()).toBe(2)
    end)

    test.it("calls sibling local functions", function()
        local function g() return 5 end
        local function f() return g() end
        test.expect(f()).toBe(5)
    end)

    test.it("supports self-recursion via local function", function()
        local function fib(n)
            if n < 2 then return n end
            return fib(n - 1) + fib(n - 2)
        end
        test.expect(fib(10)).toBe(55)
    end)
end)

test.describe("upvalue writes", function()
    test.it("mutates captured locals", function()
        local n = 0
        local function inc() n = n + 1 return n end
        test.expect(inc()).toBe(1)
        test.expect(inc()).toBe(2)
        test.expect(inc()).toBe(3)
        test.expect(n).toBe(3)
    end)

    test.it("shares one upvalue between closures", function()
        local v = 10
        local function get() return v end
        local function set(x) v = x end
        set(99)
        test.expect(get()).toBe(99)
        test.expect(v).toBe(99)
    end)

    test.it("writes through two levels of nesting", function()
        local acc = 0
        local function outer()
            local function inner() acc = acc + 5 end
            inner()
            inner()
        end
        outer()
        test.expect(acc).toBe(10)
    end)
end)

test.describe("nested capture", function()
    test.it("captures a grandparent local", function()
        local g = 3
        local function outer()
            return function() return g end
        end
        test.expect(outer()()).toBe(3)
    end)

    test.it("captures table fields' owner", function()
        local function build()
            local t = {}
            function t.get() return t end
            return t
        end
        local t = build()
        test.expect(t.get()).toBe(t)
    end)

    test.it("captures self in methods", function()
        local obj = { v = 7 }
        function obj.read(self)
            return function() return self.v end
        end
        test.expect(obj:read()()).toBe(7)
    end)
end)

test.describe("closure lifetime", function()
    test.it("outlives the defining scope (read)", function()
        local function scope()
            local hidden = "secret"
            return function() return hidden end
        end
        test.expect(scope()()).toBe("secret")
    end)

    test.it("outlives the defining scope (write)", function()
        local function scope()
            local count = 0
            return function() count = count + 1 return count end
        end
        local c = scope()
        c()
        test.expect(c()).toBe(2)
    end)

    test.it("module pattern: harness-style expect", function()
        local M = {}
        function M.expect(v)
            local e = {}
            function e.get() return v end
            return e
        end
        test.expect(M.expect(123).get()).toBe(123)
    end)
end)

test.describe("loop capture freshness", function()
    test.it("numeric for gives each iteration its own variable", function()
        local t = {}
        for i = 1, 3 do
            t[i] = function() return i end
        end
        test.expect(t[1]()).toBe(1)
        test.expect(t[2]()).toBe(2)
        test.expect(t[3]()).toBe(3)
    end)

    test.it("while-body locals are fresh per iteration", function()
        local fns = {}
        local i = 1
        while i <= 3 do
            local v = i * 10
            fns[i] = function() return v end
            i = i + 1
        end
        test.expect(fns[1]()).toBe(10)
        test.expect(fns[2]()).toBe(20)
        test.expect(fns[3]()).toBe(30)
    end)

    test.it("generic for gives fresh captures", function()
        local fns = {}
        for i, v in ipairs({ "a", "b", "c" }) do
            fns[i] = function() return v end
        end
        test.expect(fns[1]()).toBe("a")
        test.expect(fns[2]()).toBe("b")
        test.expect(fns[3]()).toBe("c")
    end)

    test.it("repeat-until body locals are fresh", function()
        local fns = {}
        local i = 1
        repeat
            local v = i
            fns[i] = function() return v end
            i = i + 1
        until i > 3
        test.expect(fns[1]()).toBe(1)
        test.expect(fns[3]()).toBe(3)
    end)

    test.it("captures made before break keep their value", function()
        local f
        for i = 1, 10 do
            if i == 4 then
                f = function() return i end
                break
            end
        end
        test.expect(f()).toBe(4)
    end)

    test.it("mutates an outer accumulator from a loop closure", function()
        local sum = 0
        local fns = {}
        for i = 1, 3 do
            fns[i] = function() sum = sum + i end
        end
        fns[1]()
        fns[2]()
        fns[3]()
        test.expect(sum).toBe(6)
    end)
end)

test.describe("shadowing", function()
    test.it("closures see the binding at definition time", function()
        local x = 1
        local first = function() return x end
        local x = 2
        local second = function() return x end
        test.expect(first()).toBe(1)
        test.expect(second()).toBe(2)
    end)
end)

test.describe("argument passing across the frame boundary", function()
    test.it("extra args to fixed-arity functions are discarded", function()
        local function fixed(a, b) return a, b end
        local x, y = fixed(1, 2, 3, 4)
        test.expect(x).toBe(1)
        test.expect(y).toBe(2)
    end)

    test.it("missing args nil-fill", function()
        local function fixed(a, b, c) return c end
        test.expect(fixed(1)).toBe(nil)
    end)

    test.it("select('#') counts varargs exactly", function()
        local function va(...) return select("#", ...) end
        test.expect(va(10, 20, 30)).toBe(3)
        test.expect(va()).toBe(0)
        test.expect(va(nil)).toBe(1)
        local function mixed(a, ...) return select("#", ...) end
        test.expect(mixed(1, 2, 3)).toBe(2)
        test.expect(mixed(1)).toBe(0)
    end)

    test.it("varargs forward through nested calls", function()
        local function inner(...) return select("#", ...), ... end
        local function outer(...) return inner(...) end
        local n, a, b = outer("p", "q")
        test.expect(n).toBe(2)
        test.expect(a).toBe("p")
        test.expect(b).toBe("q")
    end)

    test.it("coroutine resume passes args into fixed and vararg frames", function()
        local co = coroutine.create(function(x, y)
            local a, b = coroutine.yield(x + y)
            return a * b
        end)
        local ok, v = coroutine.resume(co, 3, 4)
        test.expect(ok).toBeTrue()
        test.expect(v).toBe(7)
        ok, v = coroutine.resume(co, 5, 6)
        test.expect(ok).toBeTrue()
        test.expect(v).toBe(30)
    end)

    test.it("errors unwind through mixed vararg/fixed frames", function()
        local function boom(...) error("kaput") end
        local function relay(a) return boom(a, a) end
        local ok, err = pcall(relay, 1)
        test.expect(ok).toBe(false)
        test.expect(string.find(err, "kaput") ~= nil).toBeTrue()
    end)
end)

assert(test.run())
