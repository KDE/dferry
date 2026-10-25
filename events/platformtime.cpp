// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "platformtime_p.h"

#ifdef _WIN32
// GetTickCount64() requires Vista or greater which is NT version 0x0600
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#elif defined(__linux__)
#include <time.h>
#else
#include <chrono>
#endif

namespace PlatformTime
{

uint64 monotonicMsecs()
{
#ifdef _WIN32
    return GetTickCount64();
#elif defined(__linux__)
    timespec tspec;
    // performance note: at least on Linux AMD64, clock_gettime(CLOCK_MONOTONIC) does not (usually?)
    // make a syscall, so it's surprisingly cheap; presumably it uses some built-in CPU timer feature
    clock_gettime(CLOCK_MONOTONIC, &tspec);
    return uint64(tspec.tv_sec) * 1000 + uint64(tspec.tv_nsec) / 1000000;
#else
    auto ret = uint64(std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
    return ret;
#endif
}

}

#endif // RUNNING_DOXYGEN
