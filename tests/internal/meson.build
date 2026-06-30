# MicroLua Internal Tests (C-based)

# Simple test framework helper
test_inc = include_directories('../../src')

# Tests need same MLUA_PTR_SIZE as library for value representation
test_c_args = ['-DMLUA_PTR_SIZE=8']

# Test for core types and memory functions
test_core = executable('test_core', 'TestCore.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestCore', test_core)

# Test for value representation
test_value = executable('test_value', 'TestValue.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestValue', test_value)

# Test for heap allocator
test_alloc = executable('test_alloc', 'TestAlloc.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestAlloc', test_alloc)

# Test for garbage collector
test_gc = executable('test_gc', 'TestGC.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestGC', test_gc)

# Test for strings
test_string = executable('test_string', 'TestString.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestString', test_string)

# Test for tables
test_table = executable('test_table', 'TestTable.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestTable', test_table)

# Test for lexer
test_lex = executable('test_lex', 'TestLex.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestLex', test_lex)

# Test for parser
test_parse = executable('test_parse', 'TestParse.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestParse', test_parse)

# Test for virtual machine
test_vm = executable('test_vm', 'TestVM.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestVM', test_vm)

# Test for standard libraries (C-level API tests)
test_lib = executable('test_lib', 'TestLib.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    c_args: test_c_args
)
test('TestLib', test_lib)

