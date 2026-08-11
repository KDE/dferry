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

#include "argumentsreader.h"
#include "arguments_p.h"

#include "basictypeio.h"
#include "error.h"
#include "malloccache_p.h"
#include "message.h"
#include "platform_p.h"

#include <cstddef>

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

class ArgumentsReader::Private
{
public:
    const Arguments::Private *m_argsPriv = nullptr;
    cstring m_signature;
    uint32 m_signaturePosition = uint32(-1);
    chunk m_data;
    uint32 m_dataPosition = 0;
    uint32 m_nilArrayNesting = 0; // this keeps track of how many nil arrays we are in
    Error m_error;
    Nesting m_nesting;

    struct ArrayInfo
    {
        uint32 dataEnd; // one past the last data byte of the array
        uint32 containedTypeBegin; // to rewind when reading the next element
    };

    struct VariantInfo
    {
        // Using these separate fields allows this to have a size of 16 bytes instead of 24
        // (without using nonstandard "pack" pragmas with weird side effects)
        char* prevSignaturePtr;
        uint32 prevSignatureLength;
        uint32 prevSignaturePosition; // need to store the old signature and parse position.
    };

    // for structs, we don't need to know more than that we are in a struct

    struct AggregateInfo
    {
        Arguments::IoState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
        union {
            ArrayInfo arr;
            VariantInfo var;
        };
    };

    // this keeps track of which aggregates we are currently in
#ifdef HAVE_BOOST
    boost::container::small_vector<AggregateInfo, 8> m_aggregateStack;
#else
    std::vector<AggregateInfo> m_aggregateStack;
#endif
};

thread_local static MallocCache<sizeof(ArgumentsReader::Private), 4> allocCache;

/** \class ArgumentsReader
    Reads arguments out of Arguments.

    \section argsreader_concepts States and errors
    During normal operation, state() describes which element is currently available for reading.
    You can then retrieve the corresponding data by calling a reader method according to that
    state. For example: IoState::Uint32 -> readUint32(), or
    IoState::BeginStruct -> beginStruct() or skipStruct().

    If the reader method called does not match the current state, the return value of the read will
    be invalid (but the attempt to read will not cause undefined behavior) and the ArgumentsReader
    will enter error state (state() == IoState::Finished, error().isValid() is true). Error state is
    sticky - it is not possible to leave it. This allows to safely read some data and only check for
    errors afterwards - if the invalid data that was read is not used, there is no problem.
    Note that recovery is possible, if somewhat slow, by copying the ArgumentsReader before entering
    error state and resuming reading using the saved ArgumentsReader. You will usually not need to do that.

    \see Arguments
*/

/** Constructs an %ArgumentsReader to read arguments from \p args.
 */
ArgumentsReader::ArgumentsReader(const Arguments &args)
   : d(new(allocCache.allocate()) Private),
     m_state(Arguments::NotStarted)
{
    d->m_argsPriv = Arguments::Private::of(&args);
    beginRead();
}

/** Constructs an %ArgumentsReader to read arguments from \p msg.

    This is a convenience constructor that just avoids calling msg->arguments().
 */
ArgumentsReader::ArgumentsReader(const Message &msg)
   : d(new(allocCache.allocate()) Private),
     m_state(Arguments::NotStarted)
{
    d->m_argsPriv = Arguments::Private::of(&msg.arguments());
    beginRead();
}

/// Takes ownership of \p other's data and makes \p other invalid.
ArgumentsReader::ArgumentsReader(ArgumentsReader &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = nullptr;
}

/// Takes ownership of \p other's data and makes \p other invalid.
void ArgumentsReader::operator=(ArgumentsReader &&other)
{
    if (&other == this) {
        return;
    }
    if (d) {
        d->~Private();
        allocCache.free(d);
    }
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = nullptr;
}

/// Copies \p other with all of its state, including read position.
ArgumentsReader::ArgumentsReader(const ArgumentsReader &other)
   : d(nullptr),
     m_state(other.m_state),
     m_u(other.m_u)
{
    if (other.d) {
        d = new(allocCache.allocate()) Private(*other.d);
    }
}

/// Copies \p other with all of its state, including read position.
void ArgumentsReader::operator=(const ArgumentsReader &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    m_u = other.m_u;
    if (d && other.d) {
        *d = *other.d;
    } else {
        ArgumentsReader temp(other);
        std::swap(d, temp.d);
    }
}

ArgumentsReader::~ArgumentsReader()
{
    if (d) {
        d->~Private();
        allocCache.free(d);
        d = nullptr;
    }
}

void ArgumentsReader::beginRead()
{
    VALID_IF(d->m_argsPriv, Error::NotAttachedToArguments);
    d->m_signature = d->m_argsPriv->m_signature;
    d->m_data = d->m_argsPriv->m_data;
    // as a slightly hacky optimizaton, we allow empty Arguments to allocate no space for d->m_buffer.
    if (d->m_signature.length) {
        VALID_IF(Arguments::isSignatureValid(d->m_signature), Error::InvalidSignature);
    }
    advanceState();
}

bool ArgumentsReader::isValid() const
{
    return d->m_argsPriv;
}

Error ArgumentsReader::error() const
{
    return d->m_error;
}

cstring ArgumentsReader::stateString() const
{
    return printableState(m_state);
}

bool ArgumentsReader::isInsideEmptyArray() const
{
    return d->m_nilArrayNesting > 0;
}

cstring ArgumentsReader::currentSignature() const
{
    return d->m_signature;
}

uint32 ArgumentsReader::currentSignaturePosition() const
{
    return d->m_signaturePosition;
}

cstring ArgumentsReader::currentSingleCompleteTypeSignature() const
{
    const uint32 startingLength = d->m_signature.length - d->m_signaturePosition;
    cstring sigCopy = { d->m_signature.ptr + d->m_signaturePosition, startingLength };
    Nesting nest;
    if (!parseSingleCompleteType(&sigCopy, &nest)) {
        // the signature should have been validated before, but e.g. in Finished state this may happen
        return cstring();
    }
    sigCopy.ptr = d->m_signature.ptr + d->m_signaturePosition;
    sigCopy.length = startingLength - sigCopy.length;
    return sigCopy;
}

void ArgumentsReader::replaceData(chunk data)
{
    VALID_IF(data.length >= d->m_dataPosition, Error::ReplacementDataIsShorter);

    ptrdiff_t offset = data.ptr - d->m_data.ptr;

    // fix up variant signature addresses occurring on the aggregate stack pointing into m_data;
    // don't touch the original (= call parameter, not variant) signature, which does not point into m_data.
    bool isMainSignature = true;
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        if (aggregate.aggregateType == Arguments::BeginVariant) {
            if (isMainSignature) {
                isMainSignature = false;
            } else {
                aggregate.var.prevSignaturePtr += offset;
            }
        }
    }
    if (!isMainSignature) {
        d->m_signature.ptr += offset;
    }

    d->m_data = data;
    if (m_state == Arguments::NeedMoreData) {
        advanceState();
    }
}

void ArgumentsReader::doReadPrimitiveType()
{
    switch(m_state) {
    case Arguments::Boolean: {
        uint32 num = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        m_u.Boolean = num == 1;
        VALID_IF(num <= 1, Error::MalformedMessageData);
        break; }
    case Arguments::Byte:
        m_u.Byte = d->m_data.ptr[d->m_dataPosition];
        break;
    case Arguments::Int16:
        m_u.Int16 = basic::readInt16(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Uint16:
        m_u.Uint16 = basic::readUint16(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Int32:
        m_u.Int32 = basic::readInt32(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Uint32:
        m_u.Uint32 = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Int64:
        m_u.Int64 = basic::readInt64(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Uint64:
        m_u.Uint64 = basic::readUint64(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::Double:
        m_u.Double = basic::readDouble(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        break;
    case Arguments::UnixFd: {
        uint32 index = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
        if (!d->m_nilArrayNesting) {
            VALID_IF(index < d->m_argsPriv->m_fileDescriptors.size(), Error::MalformedMessageData);
            m_u.Int32 = d->m_argsPriv->m_fileDescriptors[index];
        } else {
            m_u.Int32 = int32(InvalidFileDescriptor);
        }
        break; }
    default:
        assert(false);
        VALID_IF(false, Error::MalformedMessageData);
    }
}

void ArgumentsReader::doReadString(uint32 lengthPrefixSize)
{
    uint32 stringLength = 1;
    if (lengthPrefixSize == 1) {
        stringLength += d->m_data.ptr[d->m_dataPosition];
    } else {
        stringLength += basic::readUint32(d->m_data.ptr + d->m_dataPosition,
                                          d->m_argsPriv->m_isByteSwapped);
        VALID_IF(stringLength + 1 <= Arguments::MaxArrayLength, Error::MalformedMessageData);
    }
    d->m_dataPosition += lengthPrefixSize;
    if (unlikely(d->m_dataPosition + stringLength > d->m_data.length)) {
        m_state = Arguments::NeedMoreData;
        return;
    }
    m_u.String.ptr = reinterpret_cast<char *>(d->m_data.ptr) + d->m_dataPosition;
    m_u.String.length = stringLength - 1; // terminating null is not counted
    d->m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == Arguments::String) {
        isValidString = Arguments::isStringValid(cstring(m_u.String.ptr, m_u.String.length));
    } else if (m_state == Arguments::ObjectPath) {
        isValidString = Arguments::isObjectPathValid(cstring(m_u.String.ptr, m_u.String.length));
    } else if (m_state == Arguments::Signature) {
        isValidString = Arguments::isSignatureValid(cstring(m_u.String.ptr, m_u.String.length));
    }
    VALID_IF(isValidString, Error::MalformedMessageData);
}

void ArgumentsReader::advanceState()
{
    // if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData
    // we don't have to deal with invalid signatures here because they are checked beforehand EXCEPT
    // for aggregate nesting which cannot be checked using only one signature, due to variants.
    // variant signatures are only parsed while reading the data. individual variant signatures
    // ARE checked beforehand whenever we find one in this method.

    if (unlikely(m_state == Arguments::InvalidData)) { // nonrecoverable...
        return;
    }
    // can't do the following because a dict is one aggregate in our counting, but two according to
    // the spec: an array (one) containing dict entries (two)
    // assert(d->m_nesting.total() == d->m_aggregateStack.size());
    assert((d->m_nesting.total() == 0) == d->m_aggregateStack.empty());

    const uint32 savedSignaturePosition = d->m_signaturePosition;
    const uint32 savedDataPosition = d->m_dataPosition;

    d->m_signaturePosition++;
    assert(d->m_signaturePosition <= d->m_signature.length);

    // check if we are about to close any aggregate or even the whole argument list
    if (d->m_aggregateStack.empty()) {
        // TODO check if there is still data left, if so it's probably an error
        if (d->m_signaturePosition >= d->m_signature.length) {
            m_state = Arguments::Finished;
            return;
        }
    } else {
        const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case Arguments::BeginStruct:
            break; // handled later by TypeInfo knowing ')' -> EndStruct
        case Arguments::BeginVariant:
            if (d->m_signaturePosition >= d->m_signature.length) {
                m_state = Arguments::EndVariant;
                return;
            }
            break;
        case Arguments::BeginArray:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin) {
                // End of current iteration; either there are more or the array ends
                const Private::ArrayInfo &arrayInfo = aggregateInfo.arr;
                if (likely(!d->m_nilArrayNesting) && d->m_dataPosition < arrayInfo.dataEnd) {
                    // rewind to start of contained type and read the type info there
                    d->m_signaturePosition = arrayInfo.containedTypeBegin;
                    break; // proceed immediately to reading the next element in the array
                }
                // check that data end lines up exactly
                VALID_IF(d->m_dataPosition == arrayInfo.dataEnd, Error::MalformedMessageData);
                m_state = Arguments::EndArray;
                return;
            }
            break;
        case Arguments::BeginDict:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                // Almost like BeginArray, only differences are commented
                const Private::ArrayInfo &arrayInfo = aggregateInfo.arr;
                if (likely(!d->m_nilArrayNesting) && d->m_dataPosition < arrayInfo.dataEnd) {
                    d->m_dataPosition = align(d->m_dataPosition, 8); // align to dict entry
                    d->m_signaturePosition = arrayInfo.containedTypeBegin;
#ifdef WITH_DICT_ENTRY
                    d->m_signaturePosition--;
                    m_state = Arguments::EndDictEntry;
                    m_u.Uint32 = 0; // meaning: more dict entries follow (state after next is BeginDictEntry)
                    return;
#endif
                    break;
                }
#ifdef WITH_DICT_ENTRY
                m_state = Arguments::EndDictEntry;
                m_u.Uint32 = 1; // meaning: array end reached (state after next is EndDict)
                return;
#endif
                // check that data end lines up exactly
                VALID_IF(d->m_dataPosition == arrayInfo.dataEnd, Error::MalformedMessageData);
                m_state = Arguments::EndDict;
                return;
            }
            break;
        default:
            break;
        }
    }

    // for aggregate types, ty.alignment is just the alignment.
    // for primitive types, it's also the actual size.
    const TypeInfo ty = typeInfo(d->m_signature.ptr[d->m_signaturePosition]);
    m_state = ty.state();

    VALID_IF(m_state != Arguments::InvalidData, Error::MalformedMessageData);

    // check if we have enough data for the next type, and read it
    // if we're in a nil array, we are iterating only over the types without reading any data

    if (likely(!d->m_nilArrayNesting)) {
        uint32 padStart = d->m_dataPosition;
        d->m_dataPosition = align(d->m_dataPosition, ty.alignment);
        if (unlikely(d->m_dataPosition > d->m_data.length)) {
            goto out_needMoreData;
        }
        VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition), Error::MalformedMessageData);

        if (ty.isPrimitive || ty.isString) {
            if (unlikely(d->m_dataPosition + ty.alignment > d->m_data.length)) {
                goto out_needMoreData;
            }

            if (ty.isPrimitive) {
                doReadPrimitiveType();
                d->m_dataPosition += ty.alignment;
            } else {
                doReadString(ty.alignment);
                if (unlikely(m_state == Arguments::NeedMoreData)) {
                    goto out_needMoreData;
                }
            }
            return;
        }
    } else {
        if (ty.isPrimitive || ty.isString) {
            return; // nothing to do! (readFoo() will return "random" data, so don't use that data!)
        }
    }

    // now the interesting part: aggregates

    switch (m_state) {
    case Arguments::BeginStruct:
        VALID_IF(d->m_nesting.beginParen(), Error::MalformedMessageData);
        break;
    case Arguments::EndStruct:
#ifndef NDEBUG // in case the compiler is unable to optimize out the container accesses
        if (!d->m_aggregateStack.size() ||
            d->m_aggregateStack.back().aggregateType != Arguments::BeginStruct) {
            assert(false); // should never happen due to the pre-validated signature
        }
#endif
        break;

    case Arguments::BeginVariant: {
        cstring signature;
        if (unlikely(d->m_nilArrayNesting)) {
            static const char *emptyString = "";
            signature = cstring(emptyString, 0);
        } else {
            if (unlikely(d->m_dataPosition >= d->m_data.length)) {
                goto out_needMoreData;
            }
            signature.length = d->m_data.ptr[d->m_dataPosition++];
            signature.ptr = reinterpret_cast<char *>(d->m_data.ptr) + d->m_dataPosition;
            d->m_dataPosition += signature.length + 1;
            if (unlikely(d->m_dataPosition > d->m_data.length)) {
                goto out_needMoreData;
            }
            VALID_IF(Arguments::isSignatureValid(signature, Arguments::VariantSignature),
                     Error::MalformedMessageData);
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        VALID_IF(d->m_nesting.beginVariant(), Error::MalformedMessageData);

        // use m_u as temporary storage - its contents are undefined anyway in state BeginVariant
        m_u.String.ptr = signature.ptr;
        m_u.String.length = signature.length;
        break; }

    case Arguments::BeginArray: {
        // NB: Do not make non-idempotent changes to member variables before potentially going to
        //     out_needMoreData! We'll make the same change again after getting more data.
        uint32 arrayLength = 0;
        if (likely(!d->m_nilArrayNesting)) {
            if (unlikely(d->m_dataPosition + sizeof(uint32) > d->m_data.length)) {
                goto out_needMoreData;
            }
            arrayLength = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_argsPriv->m_isByteSwapped);
            VALID_IF(arrayLength <= Arguments::MaxArrayLength, Error::MalformedMessageData);
            d->m_dataPosition += sizeof(uint32);
        }

        if (d->m_signature.ptr[d->m_signaturePosition + 1] == '{') {
            m_state = Arguments::BeginDict;
        }

        uint32 dataEnd = d->m_dataPosition;
        // In case (and we don't check this) the internal type has greater alignment requirements than the
        // array index type (which aligns to 4 bytes), align to the nonexistent first element.
        // d->m_nilArrayNesting is only increased when the API client calls beginArray(), so
        // d->m_nilArrayNesting is the old state. As a side effect of that, it is possible to implement the
        // requirement that, in nested containers inside empty arrays, only the outermost array's first type
        // is used for alignment purposes.
        // TODO: unit-test this
        if (likely(!d->m_nilArrayNesting)) {
            const uint32 padStart = d->m_dataPosition;
            const uint32 alignment = m_state == Arguments::BeginDict ? uint32(StructAlignment) :
                                     typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]).alignment;
            d->m_dataPosition = align(d->m_dataPosition, alignment);
            VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition), Error::MalformedMessageData);
            dataEnd = d->m_dataPosition + arrayLength;
            if (unlikely(dataEnd > d->m_data.length)) {
                goto out_needMoreData;
            }
        }

        VALID_IF(d->m_nesting.beginArray(), Error::MalformedMessageData);
        if (m_state == Arguments::BeginDict) {
            // TODO check whether the first type is a primitive or string type! // ### isn't that already
            // checked for the main signature and / or variants, though?
            // only closed at end of dict - there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen(), Error::MalformedMessageData);
        }
        // temporarily store the future ArrayInfo::dataEnd in m_u.Uint32. used by {begin,skip}{Array,Dict}()
        m_u.Uint32 = dataEnd;
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    // we only start an array when the data for it has fully arrived (possible due to the length
    // prefix), so if we still run out of data in an array the input is invalid.
    VALID_IF(!d->m_nesting.array, Error::MalformedMessageData);
    m_state = Arguments::NeedMoreData;
    d->m_signaturePosition = savedSignaturePosition;
    d->m_dataPosition = savedDataPosition;
}

void ArgumentsReader::skipArrayOrDictSignature(bool isDict)
{
    // Note that we cannot just pass a dummy Nesting instance to parseSingleCompleteType, it must
    // actually check the nesting because an array may contain other nested aggregates. So we must
    // compensate for the already raised nesting levels from BeginArray handling in advanceState().
    d->m_nesting.endArray();
    if (isDict) {
        d->m_nesting.endParen();
        // the Reader ad-hoc parsing code moved at ahead by one to skip the '{', but parseSingleCompleteType()
        // needs to see the full dict signature, so fix it up
        d->m_signaturePosition--;
    }

    // parse the full (i.e. starting with the 'a') array (or dict) signature in order to skip it -
    // barring bugs, must have been too deep nesting inside variants if parsing fails
    cstring remainingSig(d->m_signature.ptr + d->m_signaturePosition,
                         d->m_signature.length - d->m_signaturePosition);
    VALID_IF(parseSingleCompleteType(&remainingSig, &d->m_nesting), Error::MalformedMessageData);
    d->m_signaturePosition = d->m_signature.length - remainingSig.length;

    // Compensate for pre-increment in advanceState()
    d->m_signaturePosition--;

    d->m_nesting.beginArray();
    if (isDict) {
        d->m_nesting.beginParen();
        // Compensate for code in advanceState() that kind of ignores the '}' at the end of a dict.
        // Unlike advanceState(), parseSingleCompleteType() does properly parse that one.
        d->m_signaturePosition--;
    }
}

/// Begins reading an array.
/// Required state: Arguments::BeginArray.
/// \param option An array always has a type, but may not contain any elements.
///               In case you want to read the type of an empty array, pass \p option = ReadTypesOnlyIfEmpty
///               to make one pass through the array while reading inert "null" data.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
bool ArgumentsReader::beginArray(EmptyArrayOption option)
{
    if (unlikely(m_state != Arguments::BeginArray)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = Arguments::BeginArray;
    Private::ArrayInfo &arrayInfo = aggregateInfo.arr; // also used for dict
    arrayInfo.dataEnd = m_u.Uint32;
    arrayInfo.containedTypeBegin = d->m_signaturePosition + 1;
    d->m_aggregateStack.push_back(aggregateInfo);

    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (!arrayLength) {
        d->m_nilArrayNesting++;
    }

    if (unlikely(d->m_nilArrayNesting && option == SkipIfEmpty)) {
        skipArrayOrDictSignature(false);
    }

    advanceState();
    return !d->m_nilArrayNesting;
}

void ArgumentsReader::skipArrayOrDict(bool isDict)
{
    // fast-forward the signature and data positions
    skipArrayOrDictSignature(isDict);
    d->m_dataPosition = m_u.Uint32;

    // m_state = isDict ? Arguments::EndDict : Arguments::EndArray; // nobody looks at it
    if (isDict) {
        d->m_nesting.endParen();
        d->m_signaturePosition++; // skip '}'
    }
    d->m_nesting.endArray();

    // proceed to next element
    advanceState();
}

/// Skips reading an array.
/// Skips the array (and all elements inside it). Required state: Arguments::BeginArray.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::skipArray()
{
    if (unlikely(m_state != Arguments::BeginArray)) {
        // TODO test this
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipArrayOrDict(false);
    }
}

/// Ends reading an array.
/// Required state: Arguments::EndArray.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::endArray()
{
    VALID_IF(m_state == Arguments::EndArray, Error::ReadWrongType);
    d->m_signaturePosition--; // fix up for the pre-increment of d->m_signaturePosition in advanceState()
    d->m_nesting.endArray();
    d->m_aggregateStack.pop_back();
    if (unlikely(d->m_nilArrayNesting)) {
        d->m_nilArrayNesting--;
    }
    advanceState();
}

std::pair<Arguments::IoState, chunk> ArgumentsReader::readPrimitiveArray()
{
    auto ret = std::make_pair(Arguments::InvalidData, chunk());

    if (m_state != Arguments::BeginArray) {
        return ret;
    }

    // the point of "primitive array" accessors is that the data can be just memcpy()ed, so we
    // reject anything that needs validation, including booleans

    const TypeInfo elementType = typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]);
    if (!elementType.isPrimitive || elementType.state() == Arguments::Boolean ||
        elementType.state() == Arguments::UnixFd) {
        return ret;
    }
    if (d->m_argsPriv->m_isByteSwapped && elementType.state() != Arguments::Byte) {
        return ret;
    }

    const uint32 size = m_u.Uint32 - d->m_dataPosition;
    // does the end of data line up with the end of the last data element?
    if (!isAligned(size, elementType.alignment)) {
        return ret;
    }
    if (size) {
        ret.second.ptr = d->m_data.ptr + d->m_dataPosition;
        ret.second.length = size;
    }
    // No need to change  d->m_nilArrayNesting - it can't be observed while "in" the current array

    ret.first = elementType.state();
    d->m_signaturePosition += 1;
    d->m_dataPosition = m_u.Uint32;
    m_state = Arguments::EndArray;
    d->m_nesting.endArray();

    // ... leave the array, there is nothing more to do in it
    advanceState();

    return ret;
}

Arguments::IoState ArgumentsReader::peekPrimitiveArray(EmptyArrayOption option) const
{
    // almost duplicated from readPrimitiveArray(), so keep it in sync
    if (m_state != Arguments::BeginArray) {
        return Arguments::InvalidData;
    }
    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (option == SkipIfEmpty && !arrayLength) {
        return Arguments::BeginArray;
    }
    const TypeInfo elementType = typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]);
    if (!elementType.isPrimitive || elementType.state() == Arguments::Boolean ||
        elementType.state() == Arguments::UnixFd) {
        return Arguments::BeginArray;
    }
    if (d->m_argsPriv->m_isByteSwapped && elementType.state() != Arguments::Byte) {
        return Arguments::BeginArray;
    }
    return elementType.state();
}

/// Begins reading a dict.
/// Required state: Arguments::BeginDict.
/// \param option A dict always has a key and a value type, but may not contain any elements.
///               In case you want to read the types of an empty dict, pass \p option = ReadTypesOnlyIfEmpty
///               to make one pass through the dict while reading inert "null" data.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
bool ArgumentsReader::beginDict(EmptyArrayOption option)
{
    if (unlikely(m_state != Arguments::BeginDict)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }

    d->m_signaturePosition++; // skip '{`

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = Arguments::BeginDict;
    Private::ArrayInfo &arrayInfo = aggregateInfo.arr; // also used for dict
    arrayInfo.dataEnd = m_u.Uint32;
    arrayInfo.containedTypeBegin = d->m_signaturePosition + 1;
    d->m_aggregateStack.push_back(aggregateInfo);

    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (!arrayLength) {
        d->m_nilArrayNesting++;
    }

    if (unlikely(d->m_nilArrayNesting && option == SkipIfEmpty)) {
        skipArrayOrDictSignature(true);
#ifdef WITH_DICT_ENTRY
        const bool ret = !d->m_nilArrayNesting;
        advanceState();
        endDictEntry();
        return ret;
    }
    m_state = Arguments::BeginDictEntry;
#else
    }

    advanceState();
#endif
    return !d->m_nilArrayNesting;
}

/// Skips reading a dict.
/// Skips the dict (and all elements inside it). Required state: Arguments::BeginDict.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::skipDict()
{
    if (unlikely(m_state != Arguments::BeginDict)) {
        // TODO test this
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        d->m_signaturePosition++; // skip '{' like beginDict() does - skipArrayOrDict() expects it
        skipArrayOrDict(true);
    }
}

bool ArgumentsReader::isDictKey() const
{
    if (!d->m_aggregateStack.empty()) {
        const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        return aggregateInfo.aggregateType == Arguments::BeginDict &&
               d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin;
    }
    return false;
}

/// Ends reading a dict.
/// Leaves the dict. Required state: Arguments::EndDict.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::endDict()
{
    VALID_IF(m_state == Arguments::EndDict, Error::ReadWrongType);
    d->m_nesting.endParen();
    //d->m_signaturePosition++; // skip '}'
    //d->m_signaturePosition--; // fix up for the pre-increment of d->m_signaturePosition in advanceState()
    d->m_nesting.endArray();
    d->m_aggregateStack.pop_back();
    if (unlikely(d->m_nilArrayNesting)) {
        d->m_nilArrayNesting--;
    }
    advanceState();
}

#ifdef WITH_DICT_ENTRY
void ArgumentsReader::beginDictEntry()
{
    VALID_IF(m_state == Arguments::BeginDictEntry, Error::ReadWrongType);
    advanceState();
}

void ArgumentsReader::endDictEntry()
{
    VALID_IF(m_state == Arguments::EndDictEntry, Error::ReadWrongType);
    if (m_u.Uint32 == 0) {
        m_state = Arguments::BeginDictEntry;
    } else {
        m_state = Arguments::EndDict;
    }
}
#endif

/// Begins reading a struct.
/// Required state: Arguments::BeginStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::beginStruct()
{
    VALID_IF(m_state == Arguments::BeginStruct, Error::ReadWrongType);
    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = Arguments::BeginStruct;
    d->m_aggregateStack.push_back(aggregateInfo);
    advanceState();
}

/// Skips reading a struct.
/// Skips the struct (and all elements inside it). Required state: Arguments::BeginStruct.
/// Otherwise, enters error state.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::skipStruct()
{
    if (unlikely(m_state != Arguments::BeginStruct)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipCurrentElement();
    }
}

/// Ends reading a struct.
/// Required state: Arguments::EndStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::endStruct()
{
    VALID_IF(m_state == Arguments::EndStruct, Error::ReadWrongType);
    d->m_nesting.endParen();
    d->m_aggregateStack.pop_back();
    advanceState();
}

/// Begins reading a variant.
/// Required state:  Arguments::BeginVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::beginVariant()
{
    VALID_IF(m_state == Arguments::BeginVariant, Error::ReadWrongType);

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = Arguments::BeginVariant;
    Private::VariantInfo &variantInfo = aggregateInfo.var;
    variantInfo.prevSignaturePtr = d->m_signature.ptr;
    variantInfo.prevSignatureLength = d->m_signature.length;
    variantInfo.prevSignaturePosition = d->m_signaturePosition;
    d->m_aggregateStack.push_back(aggregateInfo);
    d->m_signature.ptr = m_u.String.ptr;
    d->m_signature.length = m_u.String.length;
    d->m_signaturePosition = uint32(-1); // we increment d->m_signaturePosition before reading a char

    advanceState();
}

/// Skips reading a variant.
/// Skips the variant (and all elements inside it). Required state: Arguments::BeginVariant.
/// Otherwise, enters error state.
void ArgumentsReader::skipVariant()
{
    if (unlikely(m_state != Arguments::BeginVariant)) {
        m_state = Arguments::InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipCurrentElement();
    }
}

/// Ends reading a variant.
/// Required state: Arguments::EndVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::endVariant()
{
    VALID_IF(m_state == Arguments::EndVariant, Error::ReadWrongType);
    d->m_nesting.endVariant();

    const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
    const Private::VariantInfo &variantInfo = aggregateInfo.var;
    d->m_signature.ptr = variantInfo.prevSignaturePtr;
    d->m_signature.length = variantInfo.prevSignatureLength;
    d->m_signaturePosition = variantInfo.prevSignaturePosition;
    d->m_aggregateStack.pop_back();

    advanceState();
}

/// Skips reading the current element.
/// If the current element is the beginning of an aggregate, skips all elements inside the aggregate as well.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsReader::skipCurrentElement()
{
    // ### We could implement a skipping fast path for more aggregates, but it would be a lot of work, so
    //     until it's proven to be a problem, just reuse what we have.

#ifndef NDEBUG
    Arguments::IoState stateOnEntry = m_state;
#endif
    int nestingLevel = 0;
    bool isDone = false;

    while (!isDone) {
        switch(state()) {
        case Arguments::Finished:
            // Okay, that's a bit weird. I guess the graceful way to handle it is do nothing in release
            // mode, and explode in debug mode in order to warn the API client.
            // (We could use a warning message facility here, make one?)
            assert(false);
            isDone = true;
            break;
        case Arguments::BeginStruct:
            beginStruct();
            nestingLevel++;
            break;
        case Arguments::EndStruct:
            endStruct();
            nestingLevel--;
            if (!nestingLevel) {
                assert(stateOnEntry == Arguments::BeginStruct);
            }
            break;
        case Arguments::BeginVariant:
            beginVariant();
            nestingLevel++;
            break;
        case Arguments::EndVariant:
            endVariant();
            nestingLevel--;
            if (!nestingLevel) {
                assert(stateOnEntry == Arguments::BeginVariant);
            }
            break;
        case Arguments::BeginArray:
            skipArray();
            break;
        case Arguments::EndArray:
            assert(stateOnEntry == Arguments::EndArray); // only way this can happen - we gracefully skip EndArray
                                                         // and DON'T decrease nestingLevel b/c it would go negative.
            endArray();
            break;
        case Arguments::BeginDict:
            skipDict();
            break;
#ifdef WITH_DICT_ENTRY
        case Arguments::BeginDictEntry:
            beginDictEntry();
            break;
        case Arguments::EndDictEntry:
            endDictEntry();
            break;
#endif
        case Arguments::EndDict:
            assert(stateOnEntry == Arguments::EndDict); // only way this can happen - we gracefully "skip" EndDict
                                                        // and DON'T decrease nestingLevel b/c it would go negative.
            endDict();
            break;
        case Arguments::Boolean:
            readBoolean();
            break;
        case Arguments::Byte:
            readByte();
            break;
        case Arguments::Int16:
            readInt16();
            break;
        case Arguments::Uint16:
            readUint16();
            break;
        case Arguments::Int32:
            readInt32();
            break;
        case Arguments::Uint32:
            readUint32();
            break;
        case Arguments::Int64:
            readInt64();
            break;
        case Arguments::Uint64:
            readUint64();
            break;
        case Arguments::Double:
            readDouble();
            break;
        case Arguments::String:
            readString();
            break;
        case Arguments::ObjectPath:
            readObjectPath();
            break;
        case Arguments::Signature:
            readSignature();
            break;
        case Arguments::UnixFd:
            readUnixFd();
            break;
        case Arguments::NeedMoreData:
            // TODO handle this properly: rewind the state to before the aggregate - or get fancy and support
            // resuming, but that is going to get really ugly
            [[fallthrough]];
        default:
            m_state = Arguments::InvalidData;
            d->m_error.setCode(Error::StateNotSkippable);
            [[fallthrough]];
        case Arguments::InvalidData:
            isDone = true;
            break;
        }
        if (!nestingLevel) {
            isDone = true;
        }
    }
}

std::vector<Arguments::IoState> ArgumentsReader::aggregateStack() const
{
    std::vector<Arguments::IoState> ret;
    ret.reserve(d->m_aggregateStack.size());
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        ret.push_back(aggregate.aggregateType);
    }
    return ret;
}

/// Returns what aggregateStack().size() would return, but faster.
uint32 ArgumentsReader::aggregateDepth() const
{
    return d->m_aggregateStack.size();
}

/// Returns what aggregateStack().back() would return, but faster.
/// If the aggregate stack is empty, returns IoState::NotStarted.
Arguments::IoState ArgumentsReader::currentAggregate() const
{
    if (d->m_aggregateStack.empty()) {
        return Arguments::NotStarted;
    }
    return d->m_aggregateStack.back().aggregateType;
}

/// \fn byte ArgumentsReader::readByte()
/// Reads a byte (8 bit unsigned int). Required state: Arguments::Byte.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn bool ArgumentsReader::readBoolean()
/// Reads a boolean value. Required state: Arguments::Boolean.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int16 ArgumentsReader::readInt16()
/// Reads a 16 bit signed int value. Required state: Arguments::Int16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint16 ArgumentsReader::readUint16()
/// Reads a 16 bit unsigned int value. Required state: Arguments::Uint16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int32 ArgumentsReader::readInt32()
/// Reads a 32 bit signed int value. Required state: Arguments::Int32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint32 ArgumentsReader::readUint32()
/// Reads a 32 bit unsigned int value. Required state: Arguments::Uint32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int64 ArgumentsReader::readInt64()
/// Reads a 64 bit signed int value. Required state: Arguments::Int64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn uint64 ArgumentsReader::readUint64()
/// Reads a 64 bit unsigned int value. Required state: Arguments::Uint64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn double ArgumentsReader::readDouble()
/// Reads a double-precision floating point value. Required state: Arguments::Double.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsReader::readString()
/// Reads a string. Required state: Arguments::String.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsReader::readObjectPath()
/// Reads a DBus object path. Required state: Arguments::ObjectPath.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn cstring ArgumentsReader::readSignature()
/// Reads a DBus type signature. Required state: Arguments::Signature.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn int32 ArgumentsReader::readUnixFd()
/// Reads a file descriptor. The numeric value will usually not be the same as on the sending
/// side, but it will refer to the same file. Required state: Arguments::UnixFd.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

