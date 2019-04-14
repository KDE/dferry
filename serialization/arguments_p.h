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

#ifndef ARGUMENTS_P_H
#define ARGUMENTS_P_H

#include "arguments.h"

#include "error.h"

class Arguments::Private
{
public:
    Private()
       : m_isByteSwapped(false),
         m_memOwnership(nullptr)
    {}

    static inline Private *get(Arguments *args) { return args->d; }

    Private(const Private &other);
    Private &operator=(const Private &other);
    void initFrom(const Private &other);
    ~Private();

    chunk m_data;
    bool m_isByteSwapped;
    byte *m_memOwnership;
    cstring m_signature;
    std::vector<int> m_fileDescriptors;
    Error m_error;
};

struct TypeInfo
{
    inline Arguments::IoState state() const { return static_cast<Arguments::IoState>(_state); }
    byte _state;
    byte alignment : 6;
    bool isPrimitive : 1;
    bool isString : 1;
};

// helper to verify the max nesting requirements of the d-bus spec
struct Nesting
{
    inline Nesting() : array(0), paren(0), variant(0) {}
    static const int arrayMax = 32;
    static const int parenMax = 32;
    static const int totalMax = 64;

    inline bool beginArray() { array++; return likely(array <= arrayMax && total() <= totalMax); }
    inline void endArray() { assert(array >= 1); array--; }
    inline bool beginParen() { paren++; return likely(paren <= parenMax && total() <= totalMax); }
    inline void endParen() { assert(paren >= 1); paren--; }
    inline bool beginVariant() { variant++; return likely(total() <= totalMax); }
    inline void endVariant() { assert(variant >= 1); variant--; }
    inline uint32 total() { return array + paren + variant; }

    uint32 array;
    uint32 paren;
    uint32 variant;
};

cstring printableState(Arguments::IoState state);
bool parseSingleCompleteType(cstring *s, Nesting *nest);

inline bool isAligned(uint32 value, uint32 alignment)
{
    assert(alignment == 8 || alignment == 4 || alignment == 2 || alignment == 1);
    return (value & (alignment - 1)) == 0;
}

const TypeInfo &typeInfo(char letterCode);

// Macros are icky, but here every use saves three lines.
// Funny condition to avoid the dangling-else problem.
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    m_state = InvalidData; d->m_error.setCode(errCode); return; }

#endif // ARGUMENTS_P_H
