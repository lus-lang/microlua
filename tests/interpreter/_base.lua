--[[
  MicroLua Interpreter Test Framework
  /tests/interpreter/_base.lua

  Simple test framework for testing MicroLua features from Lua code.

  Usage:
    local test = require("_base")

    test.describe("Feature Name", function()
      test.it("should do something", function()
        test.expect(1 + 1).toBe(2)
      end)
    end)

    test.run()
]]

local M = {}

-- Test state
local suites = {}
local currentSuite = nil
local passed = 0
local failed = 0
local errors = {}

-- Define a test suite
function M.describe(name, fn)
    currentSuite = { name = name, tests = {} }
    fn()
    suites[#suites + 1] = currentSuite
    currentSuite = nil
end

-- Define a test case
function M.it(name, fn)
    if currentSuite then
        currentSuite.tests[#currentSuite.tests + 1] = { name = name, fn = fn }
    end
end

-- Expectation builder
function M.expect(value)
    local expectation = {}

    function expectation.toBe(expected)
        if value ~= expected then
            error(string.format("Expected %s but got %s", tostring(expected), tostring(value)), 2)
        end
    end

    function expectation.toBeNil()
        if value ~= nil then
            error(string.format("Expected nil but got %s", tostring(value)), 2)
        end
    end

    function expectation.toBeTrue()
        if value ~= true then
            error(string.format("Expected true but got %s", tostring(value)), 2)
        end
    end

    function expectation.toBeFalse()
        if value ~= false then
            error(string.format("Expected false but got %s", tostring(value)), 2)
        end
    end

    function expectation.toBeTruthy()
        if not value then
            error(string.format("Expected truthy value but got %s", tostring(value)), 2)
        end
    end

    function expectation.toEqual(expected)
        -- Deep equality for tables
        local function deepEqual(a, b)
            if type(a) ~= type(b) then return false end
            if type(a) ~= "table" then return a == b end
            for k, v in pairs(a) do
                if not deepEqual(v, b[k]) then return false end
            end
            for k in pairs(b) do
                if a[k] == nil then return false end
            end
            return true
        end
        if not deepEqual(value, expected) then
            error(string.format("Expected equal values"), 2)
        end
    end

    function expectation.toMatch(pattern)
        if type(value) ~= "string" then
            error("Expected string for pattern match", 2)
        end
        if not string.find(value, pattern) then
            error(string.format("Expected '%s' to match pattern '%s'", value, pattern), 2)
        end
    end

    function expectation.toBeGreaterThan(expected)
        if value <= expected then
            error(string.format("Expected %s > %s", tostring(value), tostring(expected)), 2)
        end
    end

    function expectation.toBeLessThan(expected)
        if value >= expected then
            error(string.format("Expected %s < %s", tostring(value), tostring(expected)), 2)
        end
    end

    function expectation.toHaveLength(expected)
        local len = #value
        if len ~= expected then
            error(string.format("Expected length %d but got %d", expected, len), 2)
        end
    end

    return expectation
end

-- Run all tests
function M.run()
    print("MicroLua Interpreter Tests")
    print("==========================\n")

    for _, suite in ipairs(suites) do
        print(suite.name .. ":")
        for _, test in ipairs(suite.tests) do
            io.write("  " .. test.name .. "... ")
            local ok, err = pcall(test.fn)
            if ok then
                print("✓")
                passed = passed + 1
            else
                print("✗")
                failed = failed + 1
                errors[#errors + 1] = {
                    suite = suite.name,
                    test = test.name,
                    error = err
                }
            end
        end
        print()
    end

    print("==========================")
    print(string.format("Results: %d passed, %d failed", passed, failed))

    if #errors > 0 then
        print("\nFailures:")
        for _, e in ipairs(errors) do
            print(string.format("  %s > %s:", e.suite, e.test))
            print("    " .. tostring(e.error))
        end
    end

    return failed == 0
end

return M
