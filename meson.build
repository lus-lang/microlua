project('microlua', 'c',
    version: '1.0.0',
    default_options: ['c_std=c99', 'warning_level=3']
)

cc = meson.get_compiler('c')

# Debug mode option - enables libc and debug output
debug_mode = get_option('buildtype') == 'debug'

size_c_args = []
size_link_args = []
if not debug_mode
    if cc.has_argument('-Oz')
        size_c_args += ['-Oz']
    elif cc.has_argument('-Os')
        size_c_args += ['-Os']
    endif

    # Keep release executables small. On Darwin, -x removes local symbols from
    # the linked Mach-O and -dead_strip drops unused sections/symbols.
    if cc.has_link_argument('-Wl,-x')
        size_link_args += ['-Wl,-x']
    endif
    if cc.has_link_argument('-Wl,-dead_strip')
        size_link_args += ['-Wl,-dead_strip']
    endif
endif

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
    'src/MLuaDump.c',
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
    lib_c_args = ['-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                  '-DMLUA_PTR_SIZE=8'] + size_c_args
endif

microlua_lib = static_library('microlua', microlua_sources,
    include_directories: microlua_inc,
    c_args: lib_c_args,
    install: true
)

# REPL binary - always uses libc for I/O. It also links the OPTIONAL io/os
# extension (libc-dependent, deliberately NOT part of the freestanding core).
repl_c_args = ['-DMLUA_PTR_SIZE=8'] + (debug_mode ? [] : size_c_args)
mlua_exe = executable('mlua', ['src/MLuaRepl.c', 'src/extensions/MLuaStdLib.c'],
    include_directories: microlua_inc,
    link_with: microlua_lib,
    c_args: repl_c_args,
    link_args: size_link_args,
    install: true
)

# Tests
subdir('tests')
