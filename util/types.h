// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

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
