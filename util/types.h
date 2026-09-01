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

#if defined(_MSC_VER) && !defined(__clang__) // MSVC
#define unreachable() __assume(false)
#else // GCC, Clang
#define unreachable() __builtin_unreachable()
#endif

typedef unsigned char byte;
typedef short int int16;
typedef unsigned short int uint16;
typedef int int32;
typedef unsigned int uint; // Windows doesn't define uint by default
typedef unsigned int uint32;
typedef long long int int64;
typedef unsigned long long int uint64;

/** Block of data.
    Pointer and length in one struct.
 */
struct DFERRY_EXPORT chunk
{
    /// Constructs a null chunk.
    chunk() : ptr(nullptr), length(0) {}
    /// Constructs a chunk with pointer and length.
    chunk(byte *b, uint32 l) : ptr(b), length(l) {}
    /// Constructs a chunk with pointer and length.
    /// Convenience constructor for char buffers.
    chunk(char *b, uint32 l) : ptr(reinterpret_cast<byte *>(b)), length(l) {}

    /// Pointer to beginning of data
    byte *ptr;
    /// Length of data
    uint32 length;
};

/** UTF-8 string.
    Pointer and length in one struct.
 */
struct DFERRY_EXPORT cstring
{
    /// Constructs a null string.
    cstring() : ptr(nullptr), length(0) {}
    /// Constructs a string with pointer and length.
    cstring(char *b, uint32 l) : ptr(b), length(l) {}
    /// Constructs a string with pointer and length.
    /// Convenience constructor for byte buffers.
    cstring(byte *b, uint32 l) : ptr(reinterpret_cast<char *>(b)), length(l) {}

    /// Constructs a string with pointer and length.
    /// Convenience constructor for const char buffers. This is const-incorrect, be careful.
    cstring(const char *b, uint32 l) : ptr(const_cast<char *>(b)), length(l) {}
    /// Constructs a cstring from a null-terminated C-style string.
    /// This is const-incorrect, be careful.
    cstring(const char *b);

    /// Pointer to beginning of string
    char *ptr;
    /// Length of string, not including null terminator
    uint32 length;
};


#endif // TYPES_H
