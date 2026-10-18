#pragma once
#include <3ds.h>

/* Threads and time on the 3DS. Threads run on the application core below the
 * main thread's priority, so they get the CPU whenever the frame loop waits for
 * the next VBlank (and whenever they block on the network). */

typedef Thread sys_thread;

static inline sys_thread sys_thread_start(ThreadFunc fn, void *arg, size_t stack)
{
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    return threadCreate(fn, arg, stack, prio + 1 > 0x3F ? 0x3F : prio + 1, -2, false);
}

static inline void sys_thread_join(sys_thread *t)
{
    if (!*t) return;
    threadJoin(*t, U64_MAX);
    threadFree(*t);
    *t = NULL;
}

static inline u64 sys_ms(void) { return osGetTime(); }
static inline void sys_sleep_ms(int ms) { svcSleepThread((s64)ms * 1000000); }
