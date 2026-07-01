# MicroLua Internal Tests (C-based)

# Simple test framework helper
test_inc = include_directories('../../src')

# Tests need the same runtime configuration as the library.
test_c_args = shared_c_args

# Test for core types and memory functions
test_core = executable('test_core', 'TestCore.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestCore', test_core)

# Test for value representation
test_value = executable('test_value', 'TestValue.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestValue', test_value)

# Test for heap allocator
test_alloc = executable('test_alloc', 'TestAlloc.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestAlloc', test_alloc)

if memory_diagnostics
    test_memory_diagnostics = executable('test_memory_diagnostics',
        'TestMemoryDiagnostics.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        c_args: test_c_args
    )
    test('TestMemoryDiagnostics', test_memory_diagnostics,
        suite: 'memory')
endif

# Test for garbage collector
test_gc = executable('test_gc', 'TestGC.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestGC', test_gc)

# Test for strings
test_string = executable('test_string', 'TestString.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestString', test_string)

# Test for tables
test_table = executable('test_table', 'TestTable.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestTable', test_table)

# Fixed-width / pointer-width type invariants (prints detected widths).
test_widths = executable('test_widths', 'TestWidths.c',
    include_directories: test_inc,
    link_with: microlua_lib,
    dependencies: m_dep,
    c_args: test_c_args
)
test('TestWidths', test_widths)

# Bytecode binary64<->binary32 conversions. Compiled at binary32 (regardless of
# the build-dir's float config) so the narrowing path and wrappers are always
# exercised on the host.
test_floatbits = executable('test_floatbits', 'TestFloatBits.c',
    include_directories: test_inc,
    dependencies: m_dep,
    c_args: test_c_args + ['-DMLUA_FLOAT=float', '-DMLUA_FLOAT_BITS=32']
)
test('TestFloatBits', test_floatbits)

if compiler_enabled
    # Test for lexer
    test_lex = executable('test_lex', 'TestLex.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestLex', test_lex)

    # Test for parser
    test_parse = executable('test_parse', 'TestParse.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestParse', test_parse)

    # Test for virtual machine
    test_vm = executable('test_vm', 'TestVM.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestVM', test_vm)

    # Test for standard libraries (C-level API tests)
    test_lib = executable('test_lib', 'TestLib.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestLib', test_lib)

    # Test for coroutines and GC under memory pressure
    test_coro = executable('test_coro', 'TestCoro.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestCoro', test_coro)

    test_bytecode = executable('test_bytecode', 'TestBytecode.c',
        include_directories: test_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: test_c_args
    )
    test('TestBytecode', test_bytecode, suite: 'bytecode')
endif
