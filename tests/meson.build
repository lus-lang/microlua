# MicroLua Tests

# Include internal C-based tests
subdir('internal')

# Interpreter tests: each suite runs through the mlua executable and ends
# with assert(test.run()), so a failing case exits non-zero. The workdir
# matters: require("_base") resolves relative to it.
if compiler_enabled
    interpreter_tests = [
        'test_base',
        'test_string',
        'test_table',
        'test_closure',
        'test_coroutine',
        'test_math',
        'test_io',
        'test_operators',
        'test_bigint',
    ]
    foreach t : interpreter_tests
        test('interp_' + t, mlua_exe,
            args: [t + '.lua'],
            workdir: meson.current_source_dir() / 'interpreter',
            suite: 'interpreter',
        )
    endforeach

    smoke_tests = [
        'test_concat',
        'test_coro',
        'test_coro2',
        'test_fib',
        'test_matrix',
        'test_sieve',
        'test_sieve2',
        'test_string_pattern',
        'test_string_pattern2',
        'test_table_ops',
    ]
    foreach t : smoke_tests
        test('smoke_' + t, mlua_exe,
            args: [t + '.lua'],
            workdir: meson.current_source_dir() / 'smoke',
            suite: 'smoke',
        )
    endforeach
endif

python_prog = find_program('python3', required: true)

if compiler_enabled
    test('cli_bytecode_output', python_prog,
        args: [files('cli_bytecode.py'), mlua_exe],
        suite: 'cli',
    )

    test('bytecode_only_build', python_prog,
        args: [files('bytecode_only_build.py'), meson.project_source_root(), mlua_exe],
        suite: ['cli', 'bytecode'],
        timeout: 120,
    )

    test('security_regressions', python_prog,
        args: [files('security_regressions.py'), mlua_exe],
        suite: ['cli', 'security'],
    )
endif

# Guard: the freestanding core must stay libc-free (see CLAUDE.md). The source
# include scan runs in every build; the nm symbol scan only runs when the
# library is actually built freestanding (non-debug), since the debug build
# deliberately links libc.
libc_free_check = files('../tools/check_libc_free.py')

test('libc_free_includes', python_prog,
    args: [libc_free_check],
    suite: 'guard',
)

if not debug_mode
    test('libc_free_symbols', python_prog,
        args: [libc_free_check, microlua_lib],
        suite: 'guard',
    )
endif
