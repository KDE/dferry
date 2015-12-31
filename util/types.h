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

#ifndef TYPES_H
#define TYPES_H

#include "export.h"

// ### this belongs into a different header
#ifdef __GNUC__
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)
#else
// !!() for maximum compatibility with the non-no-op versions
#define likely(x)    !!(x)
#define unlikely(x)  !!(x)
#endif

typedef unsigned char byte;
typedef short int int16;
typedef unsigned short int uint16;
typedef int int32;
typedef unsigned int uint; // Windows doesn't define uint by default
typedef unsigned int uint32;
typedef long long int int64;
typedef unsigned long long int uint64;

struct DFERRY_EXPORT chunk
{
    chunk() : ptr(nullptr), length(0) {}
    chunk(byte *b, uint32 l) : ptr(b), length(l) {}
    chunk(char *b, uint32 l) : ptr(reinterpret_cast<byte *>(b)), length(l) {}
    chunk(const char *b, uint32 l) : ptr(reinterpret_cast<byte *>(const_cast<char *>(b))), length(l) {}
    byte *ptr;
    uint32 length;
};

struct DFERRY_EXPORT cstring
{
    cstring() : ptr(nullptr), length(0) {}
    cstring(byte *b, uint32 l) : ptr(reinterpret_cast<char *>(b)), length(l) {}
    cstring(char *b, uint32 l) : ptr(b), length(l) {}
    cstring(const char *b, uint32 l) : ptr(const_cast<char *>(b)), length(l) {}
    cstring(const char *b);
    char *ptr;
    // length does not include terminating null! (this is okay because cstring does not
    // own the memory, so the accounting usually doesn't get screwed up)
    uint32 length;
};


#endif // TYPES_H
