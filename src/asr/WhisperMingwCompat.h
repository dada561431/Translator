#pragma once
#if defined(_WIN32) && defined(__MINGW32__)
#include <windows.h>
// MinGW 13.1's winbase.h has SetThreadInformation/ThreadPowerThrottling, but
// omits this Windows SDK layout. Scoped to ggml-cpu; no upstream source edit.
#ifndef THREAD_POWER_THROTTLING_CURRENT_VERSION
#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
typedef struct _THREAD_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} THREAD_POWER_THROTTLING_STATE;
#endif
#endif
