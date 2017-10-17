/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef PLATFORM_H
#define PLATFORM_H

// Note about invalid descriptors: On Unix they are -1 signed, on Windows they are
// ~0 unsigned. Same bit pattern, not directly interchangeable in use.

#ifdef __unix__
typedef int FileDescriptor;
#endif

#ifdef _WIN32
// this is ugly, but including all of winsock2.h in lots of headers is also ugly...
#ifdef _WIN64
typedef unsigned long long int FileDescriptor;
#else
typedef unsigned int FileDescriptor;
#endif
#endif

enum InvalidFileDescriptorEnum : FileDescriptor {
#ifdef _WIN32
    InvalidFileDescriptor = ~ FileDescriptor(0)
#else
    InvalidFileDescriptor = -1
#endif
};

static inline bool isValidFileDescriptor(FileDescriptor fd)
{
#ifdef _WIN32
    return fd != InvalidFileDescriptor;
#else
    return fd >= 0;
#endif
}

#endif // PLATFORM_H
