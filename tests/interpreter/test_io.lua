--[[
  MicroLua io/os Extension Tests
  /tests/interpreter/test_io.lua

  These exercise the OPTIONAL libc-backed extension the REPL links
  (io.*, os.*, dofile, loadfile).
]]

local test = require("_base")

local TMP = (os.getenv("TMPDIR") or "/tmp") .. "/mlua_io_test.txt"

test.describe("io files", function()
    test.it("writes and reads back a file", function()
        local f = io.open(TMP, "w")
        test.expect(type(f)).toBe("table")
        f:write("alpha\n")
        f:write("beta", "-", 42, "\n")
        f:close()

        local g = io.open(TMP, "r")
        test.expect(g:read("l")).toBe("alpha")
        test.expect(g:read("l")).toBe("beta-42")
        test.expect(g:read("l")).toBeNil()
        g:close()
    end)

    test.it("reads the whole file with 'a'", function()
        local f = io.open(TMP, "r")
        local all = f:read("a")
        f:close()
        test.expect(all).toBe("alpha\nbeta-42\n")
    end)

    test.it("iterates lines", function()
        local f = io.open(TMP, "r")
        local collected = {}
        for line in f:lines() do
            collected[#collected + 1] = line
        end
        f:close()
        test.expect(#collected).toBe(2)
        test.expect(collected[1]).toBe("alpha")
        test.expect(collected[2]).toBe("beta-42")
    end)

    test.it("reports missing files", function()
        local f, err = io.open("/definitely/not/here.txt", "r")
        test.expect(f).toBeNil()
        test.expect(type(err)).toBe("string")
    end)

    test.it("close prevents further use", function()
        local f = io.open(TMP, "r")
        f:close()
        local ok = pcall(function() return f:read("l") end)
        test.expect(ok).toBe(false)
    end)
end)

test.describe("os", function()
    test.it("time and clock return numbers", function()
        test.expect(type(os.time())).toBe("number")
        test.expect(type(os.clock())).toBe("number")
    end)

    test.it("date returns a string or table", function()
        test.expect(type(os.date())).toBe("string")
        local t = os.date("*t")
        test.expect(type(t)).toBe("table")
        test.expect(t.year > 2000).toBe(true)
        test.expect(t.month >= 1 and t.month <= 12).toBe(true)
    end)

    test.it("getenv returns nil for unset variables", function()
        test.expect(os.getenv("MLUA_DEFINITELY_UNSET_VAR_42")).toBeNil()
    end)
end)

test.describe("dofile and loadfile", function()
    test.it("loadfile compiles a chunk without running it", function()
        local f = io.open(TMP, "w")
        f:write("SIDE_EFFECT = true return 7\n")
        f:close()

        SIDE_EFFECT = nil
        local chunk = loadfile(TMP)
        test.expect(type(chunk) == "function" or type(chunk) == "table")
            .toBe(true)
        test.expect(SIDE_EFFECT).toBeNil()
        local v = chunk()
        test.expect(v).toBe(7)
        test.expect(SIDE_EFFECT).toBe(true)
        SIDE_EFFECT = nil
    end)

    test.it("dofile runs a chunk and returns its results", function()
        local f = io.open(TMP, "w")
        f:write("return 11, 22\n")
        f:close()

        local a, b = dofile(TMP)
        test.expect(a).toBe(11)
        test.expect(b).toBe(22)
    end)

    test.it("loadfile reports parse errors as nil+message", function()
        local f = io.open(TMP, "w")
        f:write("this is ( not lua\n")
        f:close()

        local chunk, err = loadfile(TMP)
        test.expect(chunk).toBeNil()
        test.expect(type(err)).toBe("string")
    end)
end)

assert(test.run())
