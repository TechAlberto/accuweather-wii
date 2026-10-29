/* Minimal libogc stand-ins so source/wiiupdate.c can run on a desktop. */
#pragma once
#include <stdint.h>
#include <pthread.h>
#include "gctypes.h"
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint64_t u64;
typedef int32_t s32;
typedef pthread_t lwp_t;
#define LWP_THREAD_NULL 0
#define ATTRIBUTE_ALIGN(n) __attribute__((aligned(n)))
static inline int LWP_CreateThread(lwp_t *t, void *(*fn)(void *), void *arg, void *stack, u32 size, u8 prio)
{
    (void)stack; (void)size; (void)prio;
    return pthread_create(t, NULL, fn, arg) ? -1 : 0;
}
