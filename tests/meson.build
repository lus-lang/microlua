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
        'test_index_fused',
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

    test('gc_pressure', python_prog,
        args: [files('gc_pressure.py'), mlua_exe],
        suite: ['cli', 'gc'],
        timeout: 60,
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

# Freestanding embeddability: a minimal bring-your-own-arena + output-hook
# embedder links the static core and runs an integer-only chunk. Complements
# libc_free_symbols (no libc symbols) by proving the core actually links + runs.
# Needs the compiler for MLuaDoString.
if compiler_enabled
    embed_smoke = executable('embed_smoke', 'embed/embed_smoke.c',
        include_directories: microlua_inc,
        link_with: microlua_lib,
        dependencies: m_dep,
        c_args: shared_c_args,
    )
    test('embed_smoke', embed_smoke, suite: 'guard')
endif

# Cross-compile width canaries: verify (never assume) a nonstandard target's
# type widths from the compiler, then confirm the freestanding core compiles
# there. Skip-with-notice (exit 77) when the toolchain is absent. AVR is the
# non-32-bit-int / non-binary64-double case; riscv32 is a real 32-bit ABI.
cross_canary = files('../tools/cross_canary.py')
cross_srcroot = meson.project_source_root()

test('canary_avr', python_prog,
    args: [cross_canary, 'avr-gcc', cross_srcroot, '2', '2', '4',
           '-mmcu=atmega328p'],
    suite: 'guard',
)
test('canary_riscv32', python_prog,
    args: [cross_canary, 'riscv64-linux-gnu-gcc', cross_srcroot, '4', '4', '8',
           '-march=rv32imac', '-mabi=ilp32'],
    suite: 'guard',
)
# eZ80 (TI-84 Plus CE): 24-bit int/pointers, binary32 double. The port header
# is required — without it MLUA_FLOAT stays double while the target double is
# 4 bytes, and the float/bytecode boundary would be silently wrong. ez80-clang
# ships without default include paths (the CE makefiles pass them), so derive
# the freestanding header dir from the toolchain's own cedev-config.
canary_ez80_args = []
cedev_config = find_program('cedev-config', required: false)
if cedev_config.found()
    cedev_prefix = run_command(cedev_config, '--prefix', check: true).stdout().strip()
    canary_ez80_args = ['-nostdinc', '-isystem', cedev_prefix / 'include']
endif
test('canary_ez80', python_prog,
    args: [cross_canary, 'ez80-clang', cross_srcroot, '3', '3', '4',
           '-DMLUA_PORT_HEADER="ports/ti84ce.h"'] + canary_ez80_args,
    suite: 'guard',
)
