#ifndef BASICTYPEIO_H
#define BASICTYPEIO_H

#include "types.h"

// ### this is the dumb version for now (unrolled for possible performance gain)

// note that there are no writeFoo() methods; we just expect the receiver to deal with our byte order.

namespace basic
{

inline int16 readInt16(const byte *raw, bool swap)
{
    byte buf[2];
    if (swap) {
        buf[0] = raw[1];
        buf[1] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const int16 *>(raw);
}

inline uint16 readUint16(const byte *raw, bool swap)
{
    byte buf[2];
    if (swap) {
        buf[0] = raw[1];
        buf[1] = raw[0];
        raw = buf;
    }
    return *reinterpret_cast<const uint16 *>(raw);
}

inline int32 readInt32(const byte *raw, bool swap)
{
    byte buf[4];
    if (swap) {
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
    if (swap) {
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
    if (swap) {
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
    if (swap) {
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
    if (swap) {
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

} // namespace basic

#endif // BASICTYPEIO_H
