project('microlua', 'c',
    version: '1.0.0',
    default_options: ['c_std=c99', 'warning_level=3']
)

# Debug mode option - enables libc and debug output
debug_mode = get_option('buildtype') == 'debug'

microlua_sources = [
    'src/MLuaCore.c',
    'src/MLuaValue.c',
    'src/MLuaAlloc.c',
    'src/MLuaGC.c',
    'src/MLuaString.c',
    'src/MLuaTable.c',
    'src/MLuaUTF8.c',
    'src/MLuaLex.c',
    'src/MLuaParse.c',
    'src/MLuaCode.c',
    'src/MLuaFunc.c',
    'src/MLuaThread.c',
    'src/MLuaVM.c',
    'src/MLuaConvert.c',
    'src/library/MLuaMathLib.c',
    'src/library/MLuaCoroLib.c',
    'src/library/MLuaStringLib.c',
    'src/library/MLuaTableLib.c',
    'src/library/MLuaBaseLib.c',
]

microlua_inc = include_directories('src', 'src/library')

# Library c_args: freestanding in release, libc+debug in debug
# MLUA_PTR_SIZE must be set to enable correct NaN-boxing on 64-bit platforms
if debug_mode
    lib_c_args = ['-DMLUA_PTR_SIZE=8']
else
    lib_c_args = ['-ffreestanding', '-fno-builtin', '-DMLUA_PTR_SIZE=8']
endif

microlua_lib = static_library('microlua', microlua_sources,
    include_directories: microlua_inc,
    c_args: lib_c_args,
    install: true
)

# REPL binary - always uses libc for I/O. It also links the OPTIONAL io/os
# extension (libc-dependent, deliberately NOT part of the freestanding core).
repl_c_args = ['-DMLUA_PTR_SIZE=8']
mlua_exe = executable('mlua', ['src/MLuaRepl.c', 'src/extensions/MLuaStdLib.c'],
    include_directories: microlua_inc,
    link_with: microlua_lib,
    c_args: repl_c_args,
    install: true
)

# Tests
subdir('tests')
