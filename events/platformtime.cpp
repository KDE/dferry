/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#include "platformtime.h"

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
