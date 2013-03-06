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

// ### this belongs into a different header
#define likely(x)    __builtin_expect(!!(x), 1)
#define unlikely(x)  __builtin_expect(!!(x), 0)

typedef unsigned char byte;
typedef short int int16;
typedef unsigned short int uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int int64;
typedef unsigned long long int uint64;

struct array
{
    array() : begin(0), length(0) {}
    array(byte *b, int l) : begin(b), length(l) {}
    array(char *b, int l) : begin(reinterpret_cast<byte *>(b)), length(l) {}
    array(const char *b, int l) : begin(reinterpret_cast<byte *>(const_cast<char *>(b))), length(l) {}
    byte *begin;
    int length;
};

struct cstring
{
    cstring() : begin(0), length(0) {}
    cstring(byte *b, int l) : begin(b), length(l) {}
    cstring(char *b, int l) : begin(reinterpret_cast<byte *>(b)), length(l) {}
    cstring(const char *b, int l) : begin(reinterpret_cast<byte *>(const_cast<char *>(b))), length(l) {}
    cstring(const char *b);
    byte *begin;
    // length does not include terminating null! (this is okay because array does not
    // own the memory, so the accounting usually doesn't get screwed up)
    int length;
};


#endif // TYPES_H
