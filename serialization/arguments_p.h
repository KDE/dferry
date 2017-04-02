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

// Maximum message length is a good upper bound for maximum Arguments data length. In order to limit
// excessive memory consumption in error cases and prevent integer overflow exploits, enforce a maximum
// data length already in Arguments.
enum {
    SpecMaxArrayLength = 67108864, // 64 MiB
    SpecMaxMessageLength = 134217728 // 128 MiB
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
