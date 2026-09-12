/*
   Copyright (C) 2026 Andreas Hartmetz <ahartmetz@gmail.com>

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

// _TsnipBegin_TopDecl
// _TsnipBegin_Includes
#include "arguments_p.h"

#include "fercode_p.h"
#include "message.h"
#include "types.h"

#include <vector>
// _TsnipEnd_Includes
// _Tinsert_Includes
// _Tinsert_TestIncludes

/*
Methods that Consumer needs to implement:

// _Tinsert_ProcessArgCallbacks
*/

template<class Consumer>
class _Tvar_CgReader : public Consumer
{
public:
    explicit _Tvar_CgReader(const Arguments &args);
    explicit _Tvar_CgReader(const Message &msg);

    ~_Tvar_CgReader();

    bool readAll();

    bool isValid() const;
    Error error() const;

    Arguments::IoState state() const { return m_state; }

    bool isFinished() const { return m_state == Arguments::Finished; }
    bool isError() const { return m_state == Arguments::InvalidData; } // TODO remove?

private:
    // _Tinsert_DeclHelperMethods

    Arguments::IoState m_state;

    const byte *m_dataPtr{};
    const byte *m_dataEnd{};

    chunk m_data;
    Error m_error; // TODO use

    const std::vector<int> &m_fileDescriptors;

#if 0 // no variant support for now!
    struct VariantInfo
    {
        // TODO noexcept move ctor, maybe assignment operator?
#ifdef HAVE_BOOST
        boost::local_shared_ptr<std::vector<FerCode>> prevOps;
#else
        std::shared_ptr<std::vector<FerCode>> prevOps;
#endif
        uint32 prevOpsIndex;
    };
    std::vector<VariantInfo> m_variantStack;
#endif
};

// _TsnipBegin_Utilities
#undef VALID_IF
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    assert(false); m_state = InvalidData; d->m_error.setCode(errCode); return s_nullBuffer; }

#define VALID_IF_STATE(expectedState) if (likely(m_state == expectedState)) {} else { \
    m_state = InvalidData; d->m_error.setCode(Error::ReadWrongType); return; }

static inline const byte *align(const byte *index, uintptr_t alignment)
{
    // it also works with 1, but makes no sense
    if (alignment < 2) {
        unreachable();
    }
    const uintptr_t maxStepUp = alignment - 1;
    return reinterpret_cast<const byte *>(uintptr_t(index + maxStepUp) & ~maxStepUp);
}

static inline bool isPaddingZero(const byte *data, const byte *end)
{
    if (end - data > 7) {
        unreachable();
    }
    for (; data < end; data++) {
        if (unlikely(*data != '\0')) {
            return false;
        }
    }
    return true;
}

static const char s_nullBuffer[16] {}; // inert fake data for callers when reading bad or nonexistent data
// _TsnipEnd_Utilities
// _Tinsert_Utilities
// _Tinsert_TestHelpers

template <class Consumer>
_Tvar_CgReader<Consumer>::_Tvar_CgReader(const Arguments &args)
    : m_data(Arguments::Private::of(&args)->m_data)
    , m_fileDescriptors(args.fileDescriptors())
{
}

template <class Consumer>
_Tvar_CgReader<Consumer>::_Tvar_CgReader(const Message &msg)
    : m_data(Arguments::Private::of(&msg.arguments())->m_data)
    , m_fileDescriptors(msg.arguments().fileDescriptors())
{
}

template <class Consumer>
_Tvar_CgReader<Consumer>::~_Tvar_CgReader() = default;

template <class Consumer>
bool _Tvar_CgReader<Consumer>::isValid() const
{
    return true; // TODO
}

template <class Consumer>
Error _Tvar_CgReader<Consumer>::error() const
{
    return m_error;
}

template <class Consumer>
bool _Tvar_CgReader<Consumer>::readAll()
{
    m_dataPtr = m_data.ptr;
    m_dataEnd = m_dataPtr + m_data.length;

    // _TsnipBegin_ReadPrimitive
    {
        #define _Tvar_ReadType uint32 // _Tignore_
        #define _Tvar_PostAlign 8 // _Tignore_
        #define _Tvar_FixedAlign 8 // _Tignore_
        #define _Tvar_SpanLength 0 // _Tignore_
        // _TsnipBegin_CheckSpanLength
        if (m_dataPtr + _Tvar_SpanLength > m_dataEnd) {
            goto errorReturn;
        }
        // _TsnipEnd_CheckSpanLength
        // _Tinsert_CheckSpanLength
        const byte* newPtr = m_dataPtr + sizeof(_Tvar_ReadType);
        // _TsnipBegin_Align
        const byte *const unalignedNewPtr = newPtr;
        newPtr = align(newPtr, _Tvar_PostAlign);
        // _TsnipEnd_Align
        // _TsnipBegin_AlignFixed
        const byte *const unalignedNewPtr = newPtr;
        newPtr += _Tvar_FixedAlign;
        // _TsnipEnd_AlignFixed
        // _Tinsert_Align
        // _TsnipBegin_CheckLength
        if (newPtr > m_dataEnd) {
            goto errorReturn;
        }
        // _TsnipEnd_CheckLength
        // _Tinsert_CheckLength
        // _TsnipBegin_CheckPadding
        if (!isPaddingZero(unalignedNewPtr, newPtr)) {
            goto errorReturn;
        }
        // _TsnipEnd_CheckPadding
        // _Tinsert_CheckPadding

        const _Tvar_ReadType *ret = reinterpret_cast<const _Tvar_ReadType *>(m_dataPtr);
        m_dataPtr = newPtr;
        // _TsnipBegin_ReturnPrimitive
        Consumer::_Tvar_ProcessArgFunc(*ret);
        // _TsnipEnd_ReturnPrimitive
        // _TsnipBegin_ReturnUnixFd
        const uint32 fdIndex = *ret;
        const std::vector<int> &fdVector = m_fileDescriptors;
        int fd = -1;
        if (fdIndex < fdVector.size()) {
            fd = fdVector[fdIndex];
        }
        Consumer::_Tvar_ProcessArgFunc(fd);
        // _TsnipEnd_ReturnUnixFd
        // _Tinsert_ReturnPrimitive
    }
    // _TsnipEnd_ReadPrimitive
    // _TsnipBegin_ReadString
    {
        #define _Tvar_LengthType uint32 // _Tignore_
        const byte* newPtr = m_dataPtr + sizeof(_Tvar_LengthType);
        // _Tinsert_CheckStringLengthFieldLength

        const uint32 len = *reinterpret_cast<const _Tvar_LengthType *>(m_dataPtr);
        const char* retPtr = reinterpret_cast<const char*>(newPtr);
        newPtr += len + 1 /* trailing nul */;
        // _Tinsert_StringAlign
        if (newPtr > m_dataEnd || len + 1 > Arguments::MaxArrayLength) {
            goto errorReturn;
        }
        // _Tinsert_CheckPaddingAfterString

        // TODO? UTF-8 and object path / signature validation?

        m_dataPtr = newPtr;

        Consumer::_Tvar_ProcessArgFunc(retPtr, len);
    }
    // _TsnipEnd_ReadString
    // _TsnipBegin_ReadArray
    {
        const byte *const savedDataEnd = m_dataEnd;

        const byte* newPtr = m_dataPtr + sizeof(uint32);
        // _Tinsert_CheckArrayLengthFieldLength
        const uint32 arrayLength = *reinterpret_cast<const uint32 *>(m_dataPtr);

        // _TsnipBegin_BeforeArrayAlign
        // Note: next line is either "newPtr = align(newPtr, 8);" or "newPtr += 4" // _Tignore_
        // _Tinsert_AlignInBeforeArrayAlign
        const byte *const endPtr = newPtr + arrayLength;
        if (endPtr > m_dataEnd || arrayLength > Arguments::MaxArrayLength) {
            goto errorReturn;
        }
        if (!isPaddingZero(unalignedNewPtr, newPtr)) {
            goto errorReturn;
        }

        m_dataEnd = endPtr;
        // _TsnipEnd_BeforeArrayAlign
        // _TsnipBegin_BeforeArrayNoAlign
        const byte *const endPtr = newPtr + arrayLength;
        if (endPtr > m_dataEnd || arrayLength > Arguments::MaxArrayLength) {
            goto errorReturn;
        }

        m_dataEnd = endPtr;
        // _TsnipEnd_BeforeArrayNoAlign
        // _Tinsert_BeforeArray
        m_dataPtr = newPtr;

        if (!_Tvar_ReadArray()) {
            goto errorReturn;
        }
        m_dataEnd = savedDataEnd;
    }
    // _TsnipEnd_ReadArray
    // _Tinsert_ArgReaders

    if (m_dataPtr == m_dataEnd) {
        m_state = Arguments::Finished;
        return true;
    }
errorReturn:
    m_state = Arguments::InvalidData;
    return false;
}
// _Tinsert_DefHelperMethods
// _Tinsert_CgReadTester
// _TsnipEnd_TopDecl

// _TsnipBegin_ParseArray

template <class Consumer>
bool _Tvar_CgReader<Consumer>::_Tvar_ReadArray()
{
    if (m_dataPtr < m_dataEnd) {
        while (true) {
            // _Tinsert_ArgReaders

            if (m_dataPtr >= m_dataEnd) {
                break;
            }
            // _TsnipBegin_ArrayRepeatAlign
            {
                const byte* newPtr = m_dataPtr;
                // _Tinsert_AlignmentForArrayRepeatAlign
                if (newPtr > m_dataEnd) {
                    return false;
                }
                if (!isPaddingZero(unalignedNewPtr, newPtr)) {
                    return false;
                }
                m_dataPtr = newPtr;
            }
            // _TsnipEnd_ArrayRepeatAlign
            // _Tinsert_ArrayRepeatAlign
        }
    }

    if (m_dataPtr != m_dataEnd) {
        return false;
    }

    // _TsnipBegin_AfterArrayAlign
    // Must do this because of the convention that previous element applies alignment for next element
    {
        const byte* newPtr = m_dataPtr;
        // _Tinsert_AlignmentForAfterArrayAlign
        if (newPtr > m_dataEnd) {
            return false;
        }
        if (!isPaddingZero(unalignedNewPtr, newPtr)) {
            return false;
        }
        m_dataPtr = newPtr;
    }

    // _TsnipEnd_AfterArrayAlign
    // _Tinsert_AfterArrayAlign
    return true;

errorReturn: // for compatibility with snippets from readAll()
    return false;
}
// _TsnipEnd_ParseArray
