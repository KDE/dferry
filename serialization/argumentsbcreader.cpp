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

#include "argumentsbcreader.h"

#include "arguments.h" // TODO needed?
#include "arguments_p.h"

#include "message.h"
#include "fercode_p.h"
#include "types.h"
#include "platform_p.h"

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

#undef VALID_IF
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    m_state = Arguments::InvalidData; d->m_error.setCode(errCode); return s_nullBuffer; }

#define VALID_IF_STATE(expectedState) if (likely(m_state == expectedState)) {} else { \
    m_state = Arguments::InvalidData; d->m_error.setCode(Error::ReadWrongType); return; }


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
    if (data + 7 > end) {
        unreachable();
    }
    for (; data < end; data++) {
        if (unlikely(*data != '\0')) {
            return false;
        }
    }
    return true;
}

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

class ArgumentsBcReader::Private
{
public:
    const FerCode *m_opsPtr{};    // end pointer not needed, we stop at FerOpcode::End
    const byte *m_dataPtr{};
    const byte *m_dataEnd{};
#ifdef HAVE_BOOST
    boost::local_shared_ptr<std::vector<FerCode>> m_ops;
#else
    std::shared_ptr<std::vector<FerCode>> m_ops;
#endif
    chunk m_data; // TODO possibly replace, only operate on m_dataPtr and m_dataEnd, possibly use 32-bit pointer diffs
                  // in array operations to save space (should be fine because all in same data array with limited length)
                  // ... or just store a pointer to args and use its data in the rare cases that we need it
    Nesting m_nesting;
    Error m_error;
    // this keeps track of the limits of the current array
#ifdef HAVE_BOOST
    boost::container::small_vector<const byte *, 8> m_arrayLengthStack;
#else
    std::vector<const byte *> m_arrayLengthStack;
#endif
    std::vector<VariantInfo> m_variantStack;
    std::vector<int> m_fileDescriptors;
};

static const char s_nullBuffer[16] {}; // inert fake data for callers when reading bad or nonexistent data

/** \class ArgumentsBcReader
    Reads arguments out of Arguments using type signature byte-code.

    This class is less flexible, but generally about 2x as fast as ArgumentsReader if the bytecode
    already exists.

    \see Arguments
*/

// TODO must memoize signature -> FerCode to amortize its generation and get a real performance benefit
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, Arguments::FerEncodeOptions encodeOptions)
    : d(new Private)
{
    d->m_ops = ferCodeForSignature(args.signature(), Arguments::MethodSignature, encodeOptions);
    d->m_data = Arguments::Private::of(&args)->m_data;
    d->m_fileDescriptors = args.fileDescriptors();
    beginRead();
}

ArgumentsBcReader::ArgumentsBcReader(const Message &msg, Arguments::FerEncodeOptions encodeOptions)
    : d(new Private)
{
    const Arguments &args = msg.arguments();
    d->m_ops = ferCodeForSignature(args.signature(), Arguments::MethodSignature, encodeOptions);
    d->m_data = Arguments::Private::of(&args)->m_data;
    d->m_fileDescriptors = args.fileDescriptors();
    beginRead();
}

#ifdef HAVE_BOOST
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, boost::local_shared_ptr<std::vector<FerCode>> ferCode)
#else
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, std::shared_ptr<std::vector<FerCode>> ferCode)
#endif
    : d(new Private)
{
    d->m_ops = ferCode;
    d->m_data = Arguments::Private::of(&args)->m_data;
    d->m_fileDescriptors = args.fileDescriptors();
    beginRead();
}

#ifdef HAVE_BOOST
ArgumentsBcReader::ArgumentsBcReader(const Message &msg, boost::local_shared_ptr<std::vector<FerCode>> ferCode)
#else
ArgumentsBcReader::ArgumentsBcReader(const Message &msg, std::shared_ptr<std::vector<FerCode>> ferCode)
#endif
{
    const Arguments &args = msg.arguments();
    d->m_ops = ferCode;
    d->m_data = Arguments::Private::of(&args)->m_data;
    d->m_fileDescriptors = args.fileDescriptors();
    beginRead();
}

ArgumentsBcReader::ArgumentsBcReader(ArgumentsBcReader &&other)
    : m_state(other.m_state),
      d(other.d)
{
    other.d = nullptr;
}

ArgumentsBcReader::ArgumentsBcReader(const ArgumentsBcReader &other)
    : m_state(other.m_state),
      d(nullptr)

{
    if (other.d) {
        d = new Private(*other.d);
    }
}

void ArgumentsBcReader::operator=(ArgumentsBcReader &&other)
{
    if (&other == this) {
        return;
    }
    if (d) {
        delete d;
    }
    m_state = other.m_state;
    d = other.d;

    other.d = nullptr;
}

void ArgumentsBcReader::operator=(const ArgumentsBcReader &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    if (d && other.d) {
        *d = *other.d;
    } else {
        ArgumentsBcReader temp(other);
        std::swap(d, temp.d);
    }
}

ArgumentsBcReader::~ArgumentsBcReader()
{
    delete d;
    d = nullptr;
}

bool ArgumentsBcReader::isValid() const
{
    return true; // TODO
}

Error ArgumentsBcReader::error() const
{
    return d->m_error;
}

bool ArgumentsBcReader::beginArrayInternal(EmptyArrayOption option)
{
    (void)option; // TODO

    FerOp ferOp = d->m_opsPtr->op;
    d->m_opsPtr++;

    const byte* const lengthPtr = d->m_dataPtr;

    const byte* newPtr = d->m_dataPtr + sizeof(uint32);
    if (newPtr > d->m_dataEnd) {
        return false;
    }

    const uint32 arrayLength = *reinterpret_cast<const uint32 *>(lengthPtr);

    // Align newPtr to array contents before using it to calculate endPtr. We also need the aligned beginning
    // of the array contents for empty arrays because that is where the next data field starts. It's a quirk
    // of DBus serialization that even zero elements are aligned.
    const byte *unalignedNewPtr = newPtr;
    if (ferOp.postAlignExponent) {
        assert(ferOp.postAlignExponent == 3); // we're already 4-aligned at / after the length field
        newPtr = align(newPtr, 8);
    }

    const byte *endPtr = newPtr + arrayLength;
    if (endPtr > d->m_dataEnd || arrayLength > Arguments::MaxArrayLength) {
        return false;
    }
    // Check padding after length to make sure that padding bytes are in bounds
    if (!isPaddingZero(unalignedNewPtr, newPtr)) {
        return false;
    }

    d->m_dataPtr = newPtr;

    d->m_arrayLengthStack.push_back(d->m_dataEnd);
    // Array length becomes (until end of array) our new "data entire" length, reuses the same check
    d->m_dataEnd = endPtr;

    if (arrayLength) {
        m_state = ferOp.ioState;
        return true;

    } else {
        m_state = Arguments::EndArray;

        // Skip d->m_opsPtr to the end of the array (TODO? add skip-to-end-index data to FerCode?)
        // We need to special-case opcodes that are followed by data that doesn't *have* opcodes so that
        // we don't misinterpret it, reading bogus opcodes. Skip that data entirely.

        int skipArrayDepth = 1; // We're already inside the current array
        while (true) {
            const FerOpcode op = d->m_opsPtr->op.op;
            switch (op) {
            case FerOpcode::BeginArray:
                skipArrayDepth += 1;
                break;
            case FerOpcode::EndArray:
                skipArrayDepth -= 1;

                if (skipArrayDepth == 0) { // leaving the array at which we started
                    return false;
                }

                d->m_opsPtr++; // skip the FerRepeatArray that follows
                break;
            case FerOpcode::EnterVariant:
                d->m_opsPtr++; // skip the FerNesting that follows
                break;
            case FerOpcode::BeginVariantSignature:
            case FerOpcode::EndVariantSignature:
                // We should never run into these: BeginVariantSignature can only occur before BeginArray, and
                // EndVariantSignature can only occur after leaving the last array of the signature.
                assert(false);
                [[fallthrough]];
            default:
                break;
            }

            d->m_opsPtr++;
        }

        unreachable();
        return false; // just in case
    }
}

bool ArgumentsBcReader::beginArray(EmptyArrayOption option)
{
    if (unlikely(m_state != Arguments::BeginArray)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }
    return beginArrayInternal(option);
}

/// Ends reading an array.
/// Required state: Arguments::EndArray.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endArray()
{
    VALID_IF_STATE(Arguments::EndArray);
    advanceState();
}

bool ArgumentsBcReader::beginDict(EmptyArrayOption option)
{
    if (unlikely(m_state != Arguments::BeginDict)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }
    return beginArrayInternal(option);
}

/// Ends reading a dict.
/// Leaves the dict. Required state: Arguments::EndDict.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endDict()
{
    VALID_IF_STATE(Arguments::EndDict);
    advanceState();
}

/// Begins reading a struct.
/// Required state: Arguments::BeginStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::beginStruct()
{
    VALID_IF_STATE(Arguments::BeginStruct);
    advanceState();
}

/// Ends reading a struct.
/// Required state: Arguments::EndStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endStruct()
{
    VALID_IF_STATE(Arguments::EndStruct);
    advanceState();
}

/// Begins reading a variant.
/// Required state:  Arguments::BeginVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::beginVariant()
{
    VALID_IF_STATE(Arguments::BeginVariant);
    advanceState();
}

/// Ends reading a variant.
/// Required state: Arguments::EndVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endVariant()
{
    VALID_IF_STATE(Arguments::EndVariant);
    advanceState();
}

#ifdef WITH_DICT_ENTRY
void ArgumentsBcReader::beginDictEntry()
{
}

void ArgumentsBcReader::endDictEntry()
{
}
#endif

void ArgumentsBcReader::doReadString(uint32 lengthPrefixSize)
{
    (void)lengthPrefixSize;
}

void ArgumentsBcReader::beginRead()
{
    //std::cout << "BcBegin " << printableFerOps(*d->m_ops) << '\n';

    d->m_opsPtr = &(*d->m_ops)[0];
    m_state = d->m_opsPtr->op.ioState;
    d->m_opsPtr++;

    d->m_dataPtr = d->m_data.ptr;
    d->m_dataEnd = d->m_dataPtr + d->m_data.length;
}

const void *ArgumentsBcReader::advanceState()
{
    if (unlikely(m_state == Arguments::InvalidData || m_state == Arguments::Finished)) { // nonrecoverable...
        return s_nullBuffer;
    }

    // Normally (in release builds), no need for a length check on ops because ops are pre-validated and
    // FerOps::End marks the end.
    assert(d->m_opsPtr <= &(*d->m_ops)[0] + d->m_ops->size());
    assert(d->m_dataPtr <= d->m_dataEnd);

    FerOp ferOp = d->m_opsPtr->op;
#if 0
    std::cout << "BcAdvance opsIdx: " << uint64(d->m_opsPtr - &(*d->m_ops)[0])
              << ", op: " << ferOp.op
              << ", dataIdx: " << uint64(d->m_dataPtr - d->m_data.ptr)
              << '\n';
#endif
    d->m_opsPtr++;

    const byte* ret = d->m_dataPtr;
    const byte* newPtr = ret;

    switch (ferOp.op) {
    case FerOpcode::Copy0:
        // BeginStruct / EndStruct would be such a case. Alignment was in the previous FerOp and nothing
        // else really happens here, but we still need a new IoState "BeginStruct" for the client.
        // ### Maybe "return ret;" directly?
        break;
    case FerOpcode::Copy1:
        newPtr += 1;
        break;
    case FerOpcode::Copy2:
        newPtr += 2;
        break;
    case FerOpcode::Copy4:
        newPtr += 4;
        break;
    case FerOpcode::Copy8:
        newPtr += 8;
        break;
    case FerOpcode::String: {
        // TODO validate: utf8, trailing nul;
        // maybe do content validation after alignment to next element and data length check to avoid
        // length-checking twice. But we do need to check max string length, for String only though!
        newPtr += sizeof(uint32);
        VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
        uint32 len = *reinterpret_cast<const uint32 *>(ret);
        VALID_IF(len + 1 < Arguments::MaxArrayLength, Error::MalformedMessageData);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case FerOpcode::ObjectPath: {
        // TODO validate: utf8, trailing nul, valid object path
        newPtr += sizeof(byte);
        VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
        byte len = *reinterpret_cast<const byte *>(ret);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case FerOpcode::Signature: {
        // TODO validate: utf8, trailing nul, valid signature
        newPtr += sizeof(byte);
        VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
        byte len = *reinterpret_cast<const byte *>(ret);
        newPtr += len + 1 /* trailing nul */;
        break; }

    case FerOpcode::EnterVariant: {
        const FerNesting outerNest = d->m_opsPtr->nest;
        d->m_nesting.array += outerNest.arrayDepth;
        d->m_nesting.paren += outerNest.parenDepth;
        d->m_nesting.variant += 1;

        const uint32 opsIndex = d->m_opsPtr + 1 - &(*d->m_ops)[0]; // point it after the EnterVariant + FerNest
        d->m_variantStack.emplace_back(VariantInfo{std::move(d->m_ops), opsIndex});

        const byte len = *reinterpret_cast<const byte *>(ret);
        const cstring newSig(const_cast<byte *>(ret + sizeof(byte)), len);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;

        d->m_ops = ferCodeForSignature(newSig, Arguments::VariantSignature);
        assert(d->m_ops->size() >= 4); // two begin, one end, >= 1 data elements  // TODO error handling
        d->m_opsPtr = &(*d->m_ops)[0];

        ferOp = d->m_opsPtr->op;

        const FerNesting innerNest = (d->m_opsPtr + 1)->nest;
        const FerNesting extraNest = (d->m_opsPtr + 2)->nest;
        d->m_opsPtr += 3;
        if (d->m_nesting.array + innerNest.arrayDepth > Nesting::arrayMax ||
            d->m_nesting.paren + innerNest.parenDepth > Nesting::parenMax ||
            d->m_nesting.total() + extraNest.arrayDepth /* really combinedDeptth */ > Nesting::totalMax) {
            assert(false); // for now, TODO error handling
        }

        break; }

    case FerOpcode::EndVariantSignature: {
        assert(ferOp.postAlignExponent == 0); // alignment for next element at the end never makes sense

        VariantInfo& varInfo = d->m_variantStack.back();
        d->m_ops = std::move(varInfo.prevOps);
        d->m_opsPtr = &(*d->m_ops)[0] + varInfo.prevOpsIndex;
        d->m_variantStack.pop_back();

        const FerNesting outerNest = (d->m_opsPtr - 1)->nest;
        d->m_nesting.array -= outerNest.arrayDepth;
        d->m_nesting.paren -= outerNest.parenDepth;
        d->m_nesting.variant -= 1;

        const FerOp enterVariantOp = (d->m_opsPtr - 2)->op;
        assert(enterVariantOp.op == FerOpcode::EnterVariant);
        ferOp.postAlignExponent = enterVariantOp.postAlignExponent;
        // avoid bit fiddling by copying all bits in the first byte of FerOp - it makes no other difference
        ferOp.op = enterVariantOp.op;

        break; }

    case FerOpcode::End:
        assert(ferOp.postAlignExponent == 0); // alignment for next element at the end never makes sense
        assert(d->m_arrayLengthStack.empty());
        assert(d->m_variantStack.empty());
        assert(!d->m_nesting.array);
        assert(!d->m_nesting.paren);
        assert(!d->m_nesting.variant);
        m_state = Arguments::Finished;
        return s_nullBuffer;

    case FerOpcode::EndArray: {
        // Only handles actual end of array, "more elements to come, loop back" is handled under:
        // if (ferOp.ioState == Arguments::EndArray)
        assert(newPtr == d->m_dataEnd);

        d->m_opsPtr++; // skip endArrayData
        d->m_dataEnd = d->m_arrayLengthStack.back();
        d->m_arrayLengthStack.pop_back();
        break; }

        // The following two should only occur at the beginning of a signature, which we don't handle here
    case FerOpcode::BeginVariantSignature:
    case FerOpcode::BeginMethodSignature:
        // The following are handled by their own begin*() methods
    case FerOpcode::BeginArray:
        unreachable();
        break;
    }

    // EndArray is handled by "looking ahead" from the last element in the array so that API clients don't
    // need to call anything like nextArrayElement() to immediately read the next element.
    // Probably not great for performance!
    if (ferOp.ioState == Arguments::EndArray || ferOp.ioState == Arguments::EndDict) {
        // TODO??? the cursed nil array thing... maybe remove some conditionals by supplying an 8 byte buffer
        // of nulls to read data from and always reset to its beginning after every read.

        assert(newPtr <= d->m_dataEnd);
        // (TODO catch newPtr > d->m_dataEnd for *all* reads)

        if (newPtr < d->m_dataEnd) {
            const FerRepeatArray repeatArrayData = (d->m_opsPtr + 1)->repeatArray;

            // "another round"
            d->m_opsPtr = &(*d->m_ops)[0] + repeatArrayData.goBackOpIndex;
            assert((d->m_opsPtr - 1)->op.op == FerOpcode::BeginArray);
            if (!repeatArrayData.goBackAlignExponent) {
                // fast path
                m_state = (d->m_opsPtr - 1)->op.ioState;
                d->m_dataPtr = newPtr;
                return ret;
            } else {
                ferOp.ioState = (d->m_opsPtr - 1)->op.ioState;
                ferOp.op = FerOpcode::Copy0; // ignored, set just to avoid bit masking work (set a whole byte)
                ferOp.postAlignExponent = repeatArrayData.goBackAlignExponent;
            }
        }
    }

    m_state = ferOp.ioState;

    const byte *unalignedNewPtr;
    switch(ferOp.postAlignExponent) {
    case 0:
        // fast path for this hopefully common case
        VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
        d->m_dataPtr = newPtr;
        return ret;

    case 1:
        unalignedNewPtr = newPtr;
        newPtr = align(newPtr, 2);
        break;
    case 2:
        unalignedNewPtr = newPtr;
        newPtr = align(newPtr, 4);
        break;
    case 3:
        unalignedNewPtr = newPtr;
        newPtr = align(newPtr, 8);
        break;
    default:
        unreachable();
    }

    VALID_IF(newPtr <= d->m_dataEnd && isPaddingZero(unalignedNewPtr, newPtr),
             Error::MalformedMessageData);

    d->m_dataPtr = newPtr;
    return ret;
}

/// \fn byte ArgumentsBcReader::readByte()
/// Reads a byte (8 bit unsigned int). Required state: Arguments::Byte.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn bool ArgumentsBcReader::readBoolean()
/// Reads a boolean value. Required state: Arguments::Boolean.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int16 ArgumentsBcReader::readInt16()
/// Reads a 16 bit signed int value. Required state: Arguments::Int16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint16 ArgumentsBcReader::readUint16()
/// Reads a 16 bit unsigned int value. Required state: Arguments::Uint16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int32 ArgumentsBcReader::readInt32()
/// Reads a 32 bit signed int value. Required state: Arguments::Int32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint32 ArgumentsBcReader::readUint32()
/// Reads a 32 bit unsigned int value. Required state: Arguments::Uint32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int64 ArgumentsBcReader::readInt64()
/// Reads a 64 bit signed int value. Required state: Arguments::Int64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint64 ArgumentsBcReader::readUint64()
/// Reads a 64 bit unsigned int value. Required state: Arguments::Uint64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn double ArgumentsBcReader::readDouble()
/// Reads a double-precision floating point value. Required state: Arguments::Double.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsBcReader::readString()
/// Reads a string. Required state: Arguments::String.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsBcReader::readObjectPath()
/// Reads a DBus object path. Required state: Arguments::ObjectPath.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsBcReader::readSignature()
/// Reads a DBus type signature. Required state: Arguments::Signature.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// Reads a file descriptor. The numeric value will usually not be the same as on the sending
/// side, but it will refer to the same file. Required state: Arguments::UnixFd.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
int ArgumentsBcReader::readUnixFd()
{
    const uint32 index = readUint32();
    if (index < d->m_fileDescriptors.size()) {
        return d->m_fileDescriptors[index];
    }
    return InvalidFileDescriptor;
}
