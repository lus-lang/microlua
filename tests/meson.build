# MicroLua Tests

# Include internal C-based tests
subdir('internal')

# Interpreter tests: each suite runs through the mlua executable and ends
# with assert(test.run()), so a failing case exits non-zero. The workdir
# matters: require("_base") resolves relative to it.
interpreter_tests = [
    'test_base',
    'test_string',
    'test_table',
    'test_closure',
    'test_coroutine',
    'test_math',
    'test_io',
]
foreach t : interpreter_tests
    test('interp_' + t, mlua_exe,
        args: [t + '.lua'],
        workdir: meson.current_source_dir() / 'interpreter',
        suite: 'interpreter',
    )
endforeach
