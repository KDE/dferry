// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

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
    static inline const Private *of(const Arguments *args) { return args->d; }

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
    inline Nesting() = default;
    static constexpr int arrayMax = 32;
    static constexpr int parenMax = 32;
    static constexpr int totalMax = 64;

    inline bool beginArray() { array++; return likely(array <= arrayMax && total() <= totalMax); }
    inline void endArray() { assert(array >= 1); array--; }
    inline bool beginParen() { paren++; return likely(paren <= parenMax && total() <= totalMax); }
    inline void endParen() { assert(paren >= 1); paren--; }
    inline bool beginVariant() { variant++; return likely(total() <= totalMax); }
    inline void endVariant() { assert(variant >= 1); variant--; }
    inline uint32 total() const { return array + paren + variant; }

    uint32 array = 0;
    uint32 paren = 0;
    uint32 variant = 0;
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
    m_state = Arguments::Finished; d->mergeError(errCode); return; }

#endif // ARGUMENTS_P_H
