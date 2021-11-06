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

#ifndef BASICTYPEIO_H
#define BASICTYPEIO_H

#include "types.h"

#include <algorithm> // for std::min on Windows...
#include <cstring>

static inline uint32 align(uint32 index, uint32 alignment)
{
    const uint32 maxStepUp = alignment - 1;
    return (index + maxStepUp) & ~maxStepUp;
}

static inline bool isPaddingZero(const chunk &buffer, uint32 padStart, uint32 padEnd)
{
    padEnd = std::min(padEnd, buffer.length);
    for (; padStart < padEnd; padStart++) {
        if (unlikely(buffer.ptr[padStart] != '\0')) {
            return false;
        }
    }
    return true;
}

static inline uint32 zeroPad(byte *buffer, uint32 alignment, uint32 bufferPos)
{
    const uint32 padEnd = align(bufferPos, alignment);
    memset(buffer + bufferPos, 0, padEnd - bufferPos);
    return padEnd;
}

// ### this is the dumb version for now (unrolled for possible performance gain)

// note that there are no byte-swapping writeFoo() methods;
// we just expect the receiver to deal with our byte order.

// we could add alignment assertions here to make extra sure that it's correct

namespace basic
{

inline int16 readInt16(const byte *raw, bool swap)
{
    byte buf[2];
    if (unlikely(swap)) {
        buf[0] = raw[1];
        buf[1] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const int16 *>(raw);
}

inline uint16 readUint16(const byte *raw, bool swap)
{
    byte buf[2];
    if (unlikely(swap)) {
        buf[0] = raw[1];
        buf[1] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const uint16 *>(raw);
}

inline int32 readInt32(const byte *raw, bool swap)
{
    byte buf[4];
    if (unlikely(swap)) {
        buf[0] = raw[3];
        buf[1] = raw[2];
        buf[2] = raw[1];
        buf[3] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const int32 *>(raw);
}

inline uint32 readUint32(const byte *raw, bool swap)
{
    byte buf[4];
    if (unlikely(swap)) {
        buf[0] = raw[3];
        buf[1] = raw[2];
        buf[2] = raw[1];
        buf[3] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const uint32 *>(raw);
}

inline int64 readInt64(const byte *raw, bool swap)
{
    byte buf[8];
    if (unlikely(swap)) {
        buf[0] = raw[7];
        buf[1] = raw[6];
        buf[2] = raw[5];
        buf[3] = raw[4];
        buf[4] = raw[3];
        buf[5] = raw[2];
        buf[6] = raw[1];
        buf[7] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const int64 *>(raw);
}

inline uint64 readUint64(const byte *raw, bool swap)
{
    byte buf[8];
    if (unlikely(swap)) {
        buf[0] = raw[7];
        buf[1] = raw[6];
        buf[2] = raw[5];
        buf[3] = raw[4];
        buf[4] = raw[3];
        buf[5] = raw[2];
        buf[6] = raw[1];
        buf[7] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const uint64 *>(raw);
}

inline double readDouble(const byte *raw, bool swap)
{
    byte buf[8];
    if (unlikely(swap)) {
        buf[0] = raw[7];
        buf[1] = raw[6];
        buf[2] = raw[5];
        buf[3] = raw[4];
        buf[4] = raw[3];
        buf[5] = raw[2];
        buf[6] = raw[1];
        buf[7] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const double *>(raw);
}

inline void writeInt16(byte *raw, int16 i)
{
    *reinterpret_cast<int16 *>(raw) = i;
}

inline void writeUint16(byte *raw, uint16 i)
{
    *reinterpret_cast<uint16 *>(raw) = i;
}

inline void writeInt32(byte *raw, int32 i)
{
    *reinterpret_cast<int32 *>(raw) = i;
}

inline void writeUint32(byte *raw, uint32 i)
{
    *reinterpret_cast<uint32 *>(raw) = i;
}

inline void writeInt64(byte *raw, int64 i)
{
    *reinterpret_cast<int64 *>(raw) = i;
}

inline void writeUint64(byte *raw, uint64 i)
{
    *reinterpret_cast<uint64 *>(raw) = i;
}

inline void writeDouble(byte *raw, double d)
{
    *reinterpret_cast<double *>(raw) = d;
}

} // namespace basic

#endif // BASICTYPEIO_H
