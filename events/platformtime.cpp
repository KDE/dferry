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

#include <time.h>

namespace PlatformTime
{

uint64 monotonicMsecs()
{
    // POSIX implementation
    timespec tspec;
    // performance note: at least on Linux AMD64, clock_gettime(CLOCK_MONOTONIC) does not (usually?)
    // make a syscall, so it's surprisingly cheap; presumably it uses some built-in CPU timer feature
    clock_gettime(CLOCK_MONOTONIC, &tspec);
    return uint64(tspec.tv_sec) * 1000 + tspec.tv_nsec / 1000000;
}

}
