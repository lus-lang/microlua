/*
 * MicroLua - MLuaConfig.h
 * Central port/build configuration.
 */

#ifndef MLUA_CONFIG_H
#define MLUA_CONFIG_H

#ifdef MLUA_PORT_HEADER
#include MLUA_PORT_HEADER
#endif

#ifndef MLUA_ENABLE_COMPILER
#define MLUA_ENABLE_COMPILER 1
#endif

#ifndef MLUA_PTR_SIZE
#if defined(__LP64__) || defined(_WIN64)
#define MLUA_PTR_SIZE 8
#else
#define MLUA_PTR_SIZE 4
#endif
#endif

#ifndef MLUA_ALIGNMENT
#define MLUA_ALIGNMENT 8
#endif

#ifndef MLUA_DEFAULT_STACK_SIZE
#define MLUA_DEFAULT_STACK_SIZE 256
#endif

#ifndef MLUA_DEFAULT_ARGS_SIZE
#define MLUA_DEFAULT_ARGS_SIZE 64
#endif

#ifndef MLUA_DEFAULT_FRAMES_SIZE
#define MLUA_DEFAULT_FRAMES_SIZE 64
#endif

#ifndef MLUA_THREAD_EVAL_SIZE
#define MLUA_THREAD_EVAL_SIZE 64
#endif

#ifndef MLUA_THREAD_LOCALS_SIZE
#define MLUA_THREAD_LOCALS_SIZE 64
#endif

#ifndef MLUA_THREAD_ARGS_SIZE
#define MLUA_THREAD_ARGS_SIZE 32
#endif

#ifndef MLUA_THREAD_FRAMES_SIZE
#define MLUA_THREAD_FRAMES_SIZE 16
#endif

#ifndef MLUA_DEFAULT_GC_THRESHOLD_PERCENT
#define MLUA_DEFAULT_GC_THRESHOLD_PERCENT 75
#endif

#ifndef MLUA_ALIGNAS
#if defined(__GNUC__) || defined(__clang__)
#define MLUA_ALIGNAS(n) __attribute__((aligned(n)))
#elif defined(_MSC_VER)
#define MLUA_ALIGNAS(n) __declspec(align(n))
#else
#define MLUA_ALIGNAS(n)
#endif
#endif

#endif /* MLUA_CONFIG_H */
