--[[
  MicroLua Coroutine & Multi-Result Tests
  /tests/interpreter/test_coroutine.lua
]]

-- Framework-independent smoke checks (the harness depends on pcall, which
-- shares machinery with coroutines)
do
    local co = coroutine.create(function(a)
        local x = coroutine.yield(a + 1)
        return 99, x
    end)
    local ok, v = coroutine.resume(co, 5)
    assert(ok and v == 6, "smoke: first resume yields a+1")
    local ok2, r1, r2 = coroutine.resume(co, "hi")
    assert(ok2 and r1 == 99 and r2 == "hi", "smoke: resume arg becomes yield result")
    assert(coroutine.status(co) == "dead", "smoke: dead after return")
end

local test = require("_base")

test.describe("coroutine lifecycle", function()
    test.it("starts suspended", function()
        local co = coroutine.create(function() end)
        test.expect(coroutine.status(co)).toBe("suspended")
    end)

    test.it("runs to completion", function()
        local co = coroutine.create(function() return 42 end)
        local ok, v = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(v).toBe(42)
        test.expect(coroutine.status(co)).toBe("dead")
    end)

    test.it("rejects resuming a dead coroutine", function()
        local co = coroutine.create(function() end)
        coroutine.resume(co)
        local ok, err = coroutine.resume(co)
        test.expect(ok).toBe(false)
        test.expect(err).toMatch("dead")
    end)

    test.it("first resume passes arguments as parameters", function()
        local co = coroutine.create(function(a, b, c) return a + b + c end)
        local ok, sum = coroutine.resume(co, 1, 2, 3)
        test.expect(ok).toBe(true)
        test.expect(sum).toBe(6)
    end)
end)

test.describe("yield and resume transfer", function()
    test.it("yields values to the resumer", function()
        local co = coroutine.create(function()
            coroutine.yield(1, 2)
        end)
        local ok, a, b = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(a).toBe(1)
        test.expect(b).toBe(2)
    end)

    test.it("resume arguments become yield results", function()
        local got
        local co = coroutine.create(function()
            got = coroutine.yield()
        end)
        coroutine.resume(co)
        coroutine.resume(co, "handed-over")
        test.expect(got).toBe("handed-over")
    end)

    test.it("drives a producer loop", function()
        local co = coroutine.create(function()
            for i = 1, 5 do
                coroutine.yield(i)
            end
            return "done"
        end)
        local sum = 0
        for _ = 1, 5 do
            local ok, v = coroutine.resume(co)
            test.expect(ok).toBe(true)
            sum = sum + v
        end
        test.expect(sum).toBe(15)
        local ok, fin = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(fin).toBe("done")
        test.expect(coroutine.status(co)).toBe("dead")
    end)

    test.it("yields from nested Lua call depth", function()
        local function inner(x)
            coroutine.yield(x * 2)
            return "inner-done"
        end
        local co = coroutine.create(function()
            local r = inner(21)
            return r
        end)
        local ok, v = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(v).toBe(42)
        local ok2, r = coroutine.resume(co)
        test.expect(ok2).toBe(true)
        test.expect(r).toBe("inner-done")
    end)

    test.it("preserves upvalue state across yields", function()
        local count = 0
        local co = coroutine.create(function()
            count = count + 1
            coroutine.yield()
            count = count + 10
        end)
        coroutine.resume(co)
        test.expect(count).toBe(1)
        coroutine.resume(co)
        test.expect(count).toBe(11)
    end)

    test.it("uses varargs after a yield", function()
        local co = coroutine.create(function(...)
            coroutine.yield()
            return select("#", ...)
        end)
        coroutine.resume(co, "a", "b", "c")
        local ok, n = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(n).toBe(3)
    end)
end)

test.describe("error handling", function()
    test.it("reports body errors via resume", function()
        local co = coroutine.create(function() error("boom") end)
        local ok, err = coroutine.resume(co)
        test.expect(ok).toBe(false)
        test.expect(err).toMatch("boom")
        test.expect(coroutine.status(co)).toBe("dead")
    end)

    test.it("forbids yielding from the main thread", function()
        local ok, err = pcall(coroutine.yield)
        test.expect(ok).toBe(false)
        test.expect(err).toMatch("outside a coroutine")
    end)

    test.it("forbids yielding across a C-call boundary", function()
        local co = coroutine.create(function()
            pcall(function() coroutine.yield() end)
            return "survived"
        end)
        -- The pcall'd yield fails, pcall catches it, body completes.
        local ok, r = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(r).toBe("survived")
    end)

    test.it("allows plain pcall inside a coroutine", function()
        local co = coroutine.create(function()
            local ok, v = pcall(function() return 7 end)
            return ok, v
        end)
        local rok, ok, v = coroutine.resume(co)
        test.expect(rok).toBe(true)
        test.expect(ok).toBe(true)
        test.expect(v).toBe(7)
    end)
end)

test.describe("introspection", function()
    test.it("isyieldable is false on main, true inside", function()
        test.expect(coroutine.isyieldable()).toBe(false)
        local co = coroutine.create(function()
            return coroutine.isyieldable()
        end)
        local ok, y = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(y).toBe(true)
    end)

    test.it("running reports main vs coroutine", function()
        local c, isMain = coroutine.running()
        test.expect(c).toBeNil()
        test.expect(isMain).toBe(true)
        local co = coroutine.create(function()
            local inner, m = coroutine.running()
            return inner, m
        end)
        local ok, inner, m = coroutine.resume(co)
        test.expect(ok).toBe(true)
        test.expect(inner).toBe(co)
        test.expect(m).toBe(false)
    end)

    test.it("nested coroutines: inner yield reaches the inner resumer", function()
        local inner = coroutine.create(function()
            coroutine.yield("from-inner")
            return "inner-end"
        end)
        local outer = coroutine.create(function()
            local ok, v = coroutine.resume(inner)
            -- Inner yielded to US, not to main
            coroutine.yield("outer-saw:" .. v)
            local ok2, fin = coroutine.resume(inner)
            return fin
        end)
        local ok, msg = coroutine.resume(outer)
        test.expect(ok).toBe(true)
        test.expect(msg).toBe("outer-saw:from-inner")
        local ok2, fin = coroutine.resume(outer)
        test.expect(ok2).toBe(true)
        test.expect(fin).toBe("inner-end")
    end)

    test.it("close kills a suspended coroutine", function()
        local co = coroutine.create(function()
            coroutine.yield()
            return "never"
        end)
        coroutine.resume(co)
        local ok = coroutine.close(co)
        test.expect(ok).toBe(true)
        test.expect(coroutine.status(co)).toBe("dead")
    end)
end)

test.describe("state isolation", function()
    test.it("main locals survive a deep coroutine", function()
        local keeper = "untouched"
        local co = coroutine.create(function()
            local a, b, c, d = 1, 2, 3, 4
            local function deep(n)
                local x, y = n * 2, n * 3
                if n > 0 then return deep(n - 1) end
                return x + y
            end
            return deep(5)
        end)
        coroutine.resume(co)
        test.expect(keeper).toBe("untouched")
    end)

    test.it("two coroutines do not share locals", function()
        local mk = function(base)
            return coroutine.create(function()
                local v = base
                coroutine.yield()
                return v
            end)
        end
        local c1, c2 = mk(100), mk(200)
        coroutine.resume(c1)
        coroutine.resume(c2)
        local _, v1 = coroutine.resume(c1)
        local _, v2 = coroutine.resume(c2)
        test.expect(v1).toBe(100)
        test.expect(v2).toBe(200)
    end)
end)

test.describe("multi-result semantics", function()
    test.it("adjusts results in assignment lists", function()
        local function two() return 1, 2 end
        local a, b, c = two()
        test.expect(a).toBe(1)
        test.expect(b).toBe(2)
        test.expect(c).toBeNil()
    end)

    test.it("truncates in the middle of a list", function()
        local function two() return 1, 2 end
        local a, b = two(), 9
        test.expect(a).toBe(1)
        test.expect(b).toBe(9)
    end)

    test.it("parenthesized calls give exactly one value", function()
        local function two() return 1, 2 end
        local a, b = (two())
        test.expect(a).toBe(1)
        test.expect(b).toBeNil()
    end)

    test.it("passes all results as final call arguments", function()
        local function two() return 3, 4 end
        local function sum(...)
            local t = { ... }
            return t[1] + t[2]
        end
        test.expect(sum(two())).toBe(7)
    end)

    test.it("expands varargs in table constructors", function()
        local function pack2(...)
            local t = { ... }
            return #t
        end
        test.expect(pack2(10, 20, 30)).toBe(3)
    end)

    test.it("counts varargs via select", function()
        local function n(...) return select("#", ...) end
        test.expect(n()).toBe(0)
        test.expect(n(1)).toBe(1)
        test.expect(n(1, nil, 3)).toBe(3)
    end)

    test.it("returns everything from a tail call", function()
        local function two() return 5, 6 end
        local function fwd() return two() end
        local a, b = fwd()
        test.expect(a).toBe(5)
        test.expect(b).toBe(6)
    end)

    test.it("runs deep tail recursion in constant frame space", function()
        local function loop(n)
            if n == 0 then return "bottom" end
            return loop(n - 1)
        end
        -- Frame cap is 64; only frame reuse makes 10000 levels possible.
        test.expect(loop(10000)).toBe("bottom")
    end)
end)

assert(test.run())
