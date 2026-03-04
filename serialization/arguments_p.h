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

    static inline Private *of(Arguments *args) { return args->d; }

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

enum class FerOp : byte
{
    End = 0, // TODO not sure if needed! But, may be faster than always checking a length prefix...
    Copy1 = 1,
    Copy2 = 2,
    Copy3 = 3,
    Copy4 = 4,
    Copy5 = 5,
    Copy6 = 6,
    Copy7 = 7,
    Copy8 = 8,
    Copy9 = 9,
    Copy10 = 10,
    Copy11 = 11,
    Copy12 = 12,
    Copy13 = 13,
    Copy14 = 14,
    Copy15 = 15,
    Copy16 = 16,
    Copy17 = 17,
    Copy18 = 18,
    Copy19 = 19,
    Copy20 = 20,
    Copy21 = 21,
    Copy22 = 22,
    Copy23 = 23,
    Copy24 = 24,
    Copy25 = 25,
    Copy26 = 26,
    Copy27 = 27,
    Copy28 = 28,
    Copy29 = 29,
    Copy30 = 30,
    Copy31 = 31,
    Copy32 = 32,
    Copy33 = 33,
    Copy34 = 34,
    Copy35 = 35,
    Copy36 = 36,
    Copy37 = 37,
    Copy38 = 38,
    Copy39 = 39,
    Copy40 = 40,
    Copy41 = 41,
    Copy42 = 42,
    Copy43 = 43,
    Copy44 = 44,
    Copy45 = 45,
    Copy46 = 46,
    Copy47 = 47,
    Copy48 = 48,
    Copy49 = 49,
    Copy50 = 50,
    Copy51 = 51,
    Copy52 = 52,
    Copy53 = 53,
    Copy54 = 54,
    Copy55 = 55,
    Copy56 = 56,
    Copy57 = 57,
    Copy58 = 58,
    Copy59 = 59,
    Copy60 = 60,
    Copy61 = 61,
    Copy62 = 62,
    Copy63 = 63,
    Copy64 = 64,
    Copy65 = 65,
    Copy66 = 66,
    Copy67 = 67,
    Copy68 = 68,
    Copy69 = 69,
    Copy70 = 70,
    Copy71 = 71,
    Copy72 = 72,
    Copy73 = 73,
    Copy74 = 74,
    Copy75 = 75,
    Copy76 = 76,
    Copy77 = 77,
    Copy78 = 78,
    Copy79 = 79,
    Copy80 = 80,
    Copy81 = 81,
    Copy82 = 82,
    Copy83 = 83,
    Copy84 = 84,
    Copy85 = 85,
    Copy86 = 86,
    Copy87 = 87,
    Copy88 = 88,
    Copy89 = 89,
    Copy90 = 90,
    Copy91 = 91,
    Copy92 = 92,
    Copy93 = 93,
    Copy94 = 94,
    Copy95 = 95,
    Copy96 = 96,
    Copy97 = 97,
    Copy98 = 98,
    Copy99 = 99,
    Copy100 = 100,
    Copy101 = 101,
    Copy102 = 102,
    Copy103 = 103,
    Copy104 = 104,
    Copy105 = 105,
    Copy106 = 106,
    Copy107 = 107,
    Copy108 = 108,
    Copy109 = 109,
    Copy110 = 110,
    Copy111 = 111,
    Copy112 = 112,
    Copy113 = 113,
    Copy114 = 114,
    Copy115 = 115,
    Copy116 = 116,
    Copy117 = 117,
    Copy118 = 118,
    Copy119 = 119,
    Copy120 = 120,
    Copy121 = 121,
    Copy122 = 122,
    Copy123 = 123,
    Copy124 = 124,
    Copy125 = 125,
    Copy126 = 126,
    Copy127 = 127,
    Copy128 = 128,
    Copy256 = 129,
    Copy512 = 130,
    Copy1024 = 131,
    Copy2048 = 132,
    Copy4096 = 133,
    Align2 = 140,
    Align4 = 141,
    Align8 = 142,
    String = 150, // TODO as with BeginArray (the length field is 4-aligned!)
    ObjectPath,
    Signature,
    BeginArray, // TODO figure out if this implies alignment for the length field or not - for now, I think it's
                // better to thave the alignment explicit so that it can more easily take part in alignment merging
                // and elimination optimizations!
    EndArray,   // followed by two bytes, LSB first, to tell how far to go back in the ops list to get to the beginning of
                // the array; this is also used to skip unnecessary alignment that's only needed at the beginning of the
                // array! (imagine an array of structs or 64 bit integers after a string)
    // TODO...
    BeginVariant, // followed by 3 more bytes: array, paren. variant nesting (for "dynamic" processing of contents)
    Error = 255
};

std::vector<FerOp> ferOpsForSignature(cstring signature, bool mergeContiguousCopies);

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

enum {
    StructAlignment = 8
};

const TypeInfo &typeInfo(char letterCode);

// Macros are icky, but here every use saves three lines.
// Funny condition to avoid the dangling-else problem.
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    m_state = InvalidData; d->m_error.setCode(errCode); return; }

#endif // ARGUMENTS_P_H
