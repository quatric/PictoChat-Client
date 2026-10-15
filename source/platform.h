#pragma once
/* The few libogc names the shared code uses, on top of wut. */
#include <stdint.h>
#include <stdbool.h>
#include <coreinit/time.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

static inline u64 gettime(void) { return (u64)OSGetTime(); }
#define ticks_to_millisecs(t) ((u64)OSTicksToMilliseconds(t))
#define ticks_to_microsecs(t) ((u64)OSTicksToMicroseconds(t))

/* Where the app keeps settings.txt (and test files) on the SD card */
#define SD_ROOT "fs:/vol/external01"
#define APP_DIR SD_ROOT "/wiiu/apps/PictoChat"

/* Start a thread with its own stack; 0 on success */
#include <pthread.h>
static inline int thread_start(pthread_t *t, void *(*fn)(void *), size_t stack)
{
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, stack);
    int ret = pthread_create(t, &attr, fn, NULL);
    pthread_attr_destroy(&attr);
    return ret;
}
