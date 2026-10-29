#pragma once
#include <time.h>
static inline u64 gettime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}
#define ticks_to_millisecs(t) (t)
#define diff_ticks(a, b) ((b) - (a))
