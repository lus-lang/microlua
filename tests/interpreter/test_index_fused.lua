-- Fused indexing coverage: the parser retracts `GETLOCAL t; GETLOCAL k`
-- pairs into GETTABLE_LL / SETTABLE_LL (locals 0-15) and folds trailing
-- pops into SETTABLE_POP. Every case here has a fused and an unfused
-- compilation shape; both must behave identically to the generic path.
local test = require("_base")

test.describe("fused local-local reads", function()
    test.it("reads through t[k] in expressions", function()
        local t = { 10, 20, 30 }
        local k = 2
        test.expect(t[k]).toBe(20)
        local sum = t[k] + t[k]
        test.expect(sum).toBe(40)
    end)

    test.it("as the first statement of loop bodies", function()
        local t = { 5, 6, 7 }
        local k = 1
        local acc = 0
        while k <= 3 do
            acc = acc + t[k]
            k = k + 1
        end
        test.expect(acc).toBe(18)

        acc = 0
        for i = 1, 3 do
            acc = acc + t[i]
        end
        test.expect(acc).toBe(18)

        acc = 0
        k = 1
        repeat
            acc = acc + t[k]
            k = k + 1
        until k > 3
        test.expect(acc).toBe(18)
    end)

    test.it("inside and/or expressions", function()
        local t = { false, 2, nil }
        local k = 2
        test.expect(t[k] and "yes" or "no").toBe("yes")
        local j = 1
        test.expect(t[j] and "yes" or "no").toBe("no")
        -- key that is itself an and/or result must not fuse wrongly
        local a, b = nil, 2
        test.expect(t[a or b]).toBe(2)
    end)

    test.it("nested indexing a[i][j]", function()
        local a = { { 1, 2 }, { 3, 4 } }
        local i, j = 2, 1
        test.expect(a[i][j]).toBe(3)
        a[i][j] = 30
        test.expect(a[2][1]).toBe(30)
    end)

    test.it("falls back for keys past local slot 15", function()
        local l1, l2, l3, l4, l5, l6, l7, l8 = 1, 2, 3, 4, 5, 6, 7, 8
        local l9, l10, l11, l12, l13, l14, l15, l16 = 9, 10, 11, 12, 13, 14, 15, 16
        local t = { 100, 200 }   -- slot 16
        local k = 2              -- slot 17
        test.expect(t[k]).toBe(200)
        t[k] = 201
        test.expect(t[2]).toBe(201)
        test.expect(l1 + l16).toBe(17)
    end)
end)

test.describe("fused local-local writes", function()
    test.it("stores through t[k] = v", function()
        local t = { 0, 0, 0 }
        local k = 3
        t[k] = 33
        test.expect(t[3]).toBe(33)
        test.expect(#t).toBe(3)
    end)

    test.it("append via t[k] where k == #t + 1", function()
        local t = { 1 }
        local k = 2
        t[k] = 22
        test.expect(#t).toBe(2)
        test.expect(t[2]).toBe(22)
    end)

    test.it("hole error message matches the generic path", function()
        local t = { 1 }
        local k = 3
        local ok1, err1 = pcall(function() t[k] = 9 end)
        local u = { 1 }
        local ok2, err2 = pcall(function() u[1 + 2] = 9 end)
        test.expect(ok1).toBeFalse()
        test.expect(ok2).toBeFalse()
        test.expect(tostring(err1)).toBe(tostring(err2))
    end)

    test.it("non-table error matches the generic path", function()
        local t = nil
        local k = 1
        local ok1, err1 = pcall(function() return t[k] end)
        local ok2, err2 = pcall(function() local x = nil return x[1 + 0] end)
        test.expect(ok1).toBeFalse()
        test.expect(ok2).toBeFalse()
        test.expect(tostring(err1)).toBe(tostring(err2))
    end)

    test.it("multi-assignment targets stay correct", function()
        local t = { 0, 0 }
        local u = { 0 }
        local i, j = 1, 2
        t[i], t[j], u[i] = 11, 22, 33
        test.expect(t[1]).toBe(11)
        test.expect(t[2]).toBe(22)
        test.expect(u[1]).toBe(33)
    end)

    test.it("constructor fields and function t.f definitions", function()
        local t = { [1] = 7, x = 8 }
        test.expect(t[1]).toBe(7)
        test.expect(t.x).toBe(8)
        local m = {}
        function m.f(v)
            return v * 2
        end
        test.expect(m.f(21)).toBe(42)
    end)
end)

test.describe("fusion vs closures and delegation", function()
    test.it("nil slot delegates through forward from a fused read", function()
        local base = { 0, 999 }
        local t = { 1, 2 }
        table.forward(t, base)
        local k = 2
        t[k] = nil
        test.expect(t[k]).toBe(999)
    end)

    test.it("captured key still reads correctly after the loop", function()
        -- exercises the last-use pass: the fused op's nibble reads must
        -- keep slot loads alive
        local t = { 10, 20, 30 }
        local k = 2
        local function get()
            return k
        end
        local v = t[k]
        test.expect(v).toBe(20)
        test.expect(get()).toBe(2)
    end)

    test.it("value capturing the key reads the pre-value key", function()
        -- t[k] = <closure that mutates k>: the store must use the value
        -- of k from BEFORE the right-hand side ran
        local t = { 0, 0, 0 }
        local k = 1
        t[k] = (function()
            k = 3
            return 77
        end)()
        test.expect(t[1]).toBe(77)
        test.expect(t[3]).toBe(0)
        test.expect(k).toBe(3)
    end)

    test.it("value capturing the table reads the pre-value table", function()
        local t = { 0 }
        local old = t
        local k = 1
        t[k] = (function()
            t = { 0 }
            return 55
        end)()
        test.expect(old[1]).toBe(55)
        test.expect(t[1]).toBe(0)
    end)

    test.it("pre-captured key is read before the value runs", function()
        local t = { 0, 0 }
        local k = 1
        local function bump()
            k = k + 1
            return 42
        end
        t[k] = bump()
        test.expect(t[1]).toBe(42)
        test.expect(t[2]).toBe(0)
        test.expect(k).toBe(2)
    end)
end)

test.describe("fused chunks survive dump and load", function()
    test.it("round-trips through string.dump", function()
        if not string.dump then
            return
        end
        local function work(n)
            local t = {}
            for i = 1, n do
                t[i] = i * i
            end
            local s = 0
            for i = 1, n do
                s = s + t[i]
            end
            return s
        end
        local dumped = string.dump(work)
        local revived = load(dumped)
        test.expect(revived(10)).toBe(work(10))
    end)
end)

assert(test.run())
