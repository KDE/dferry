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

#include "arguments.h"
#include "arguments_p.h"

#include "basictypeio.h"
#include "malloccache.h"

#include <cstring>

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

enum {
    StructAlignment = 8
};

static constexpr byte alignLog[9] = { 0, 0, 1, 0, 2, 0, 0, 0, 3 };
inline constexpr byte alignmentLog2(uint32 alignment)
{
    // The following is not constexpr in C++14, and it hasn't triggered in ages
    // assert(alignment <= 8 && (alignment < 2 || alignLog[alignment] != 0));
    return alignLog[alignment];
}

class Arguments::Writer::Private
{
public:
    Private()
       : m_signaturePosition(0),
         m_data(reinterpret_cast<byte *>(malloc(InitialDataCapacity))),
         m_dataCapacity(InitialDataCapacity),
         m_dataPosition(SignatureReservedSpace),
         m_nilArrayNesting(0)
    {
        m_signature.ptr = reinterpret_cast<char *>(m_data + 1); // reserve a byte for length prefix
        m_signature.length = 0;
    }

    Private(const Private &other);
    void operator=(const Private &other);

    void reserveData(uint32 size, IoState *state)
    {
        if (likely(size <= m_dataCapacity)) {
            return;
        }
        uint32 newCapacity = m_dataCapacity;
        do {
            newCapacity *= 2;
        } while (size > newCapacity);

        byte *const oldDataPointer = m_data;
        m_data = reinterpret_cast<byte *>(realloc(m_data, newCapacity));
        m_signature.ptr += m_data - oldDataPointer;
        m_dataCapacity = newCapacity;

        // Here, we trade off getting an ArgumentsTooLong error as early as possible for fewer
        // conditional branches in the hot path. Because only the final message length has a well-defined
        // limit, and Arguments doesn't, a precise check has limited usefulness anwyay. This is just
        // a sanity check and out of bounds access / overflow protection.

        // In most cases, callers do not need to check for errors: Very large single arguments are
        // already rejected, and we actually allocate the too large buffer to prevent out-of-bounds
        // access. Any following writing API calls will then cleanly abort due to m_state == InvalidData.
        // ### Callers DO need to check m_state before possibly overwriting it, hiding the error!
        if (newCapacity > Arguments::MaxMessageLength * 3) {
            *state = InvalidData;
            m_error.setCode(Error::ArgumentsTooLong);
        }
    }

    bool insideVariant()
    {
        return !m_queuedData.empty();
    }

    // We don't know how long a variant signature is when starting the variant, but we have to
    // insert the signature into the datastream before the data. For that reason, we need a
    // postprocessing pass to fix things up once the outermost variant is closed.
    // QueuedDataInfo stores enough information about data inside variants to be able to do
    // the patching up while respecting alignment and other requirements.
    struct QueuedDataInfo
    {
        constexpr QueuedDataInfo(byte alignment, byte size_)
            : alignmentExponent(alignmentLog2(alignment)),
              size(size_)
        {}
        byte alignment() const { return 1 << alignmentExponent; }

        byte alignmentExponent : 2; // powers of 2, so 1, 2, 4, 8
        byte size : 6; // that's up to 63
        enum SizeCode {
            LargestSize = 60,
            ArrayLengthField,
            ArrayLengthEndMark,
            VariantSignature
        };
    };

    // The parameter is not a QueuedDataInfo because the compiler doesn't seem to optimize away
    // QueuedDataInfo construction when insideVariant() is false, despite inlining.
    void maybeQueueData(byte alignment, byte size)
    {
        if (insideVariant()) {
            m_queuedData.push_back(QueuedDataInfo(alignment, size));
        }
    }

    // Caution: does not ensure that enough space is available!
    void appendBulkData(chunk data)
    {
        // Align only the first of the back-to-back data chunks - otherwise, when storing values which
        // are 8 byte aligned, the second half of an element straddling a chunk boundary
        // (QueuedDataInfo::LargestSize == 60) would start at an 8-byte aligned position (so 64)
        // instead of 60 where we want it in order to just write a contiguous block of data.
        memcpy(m_data + m_dataPosition, data.ptr, data.length);
        m_dataPosition += data.length;
        if (insideVariant()) {
            for (uint32 l = data.length; l; ) {
                uint32 chunkSize = std::min(l, uint32(QueuedDataInfo::LargestSize));
                m_queuedData.push_back(QueuedDataInfo(1, chunkSize));
                l -= chunkSize;
            }
        }
    }

    void alignData(uint32 alignment)
    {
        if (insideVariant()) {
            m_queuedData.push_back(QueuedDataInfo(alignment, 0));
        }
        zeroPad(m_data, alignment, &m_dataPosition);
    }

    uint32 m_dataElementsCountBeforeNilArray;
    uint32 m_dataPositionBeforeVariant;

    Nesting m_nesting;
    cstring m_signature;
    uint32 m_signaturePosition;

    byte *m_data;
    uint32 m_dataCapacity;
    uint32 m_dataPosition;

    int m_nilArrayNesting;
    std::vector<int> m_fileDescriptors;
    Error m_error;

    enum {
        InitialDataCapacity = 512,
        // max signature length (255) + length prefix(1) + null terminator(1), rounded up to multiple of 8
        // because that doesn't change alignment
        SignatureReservedSpace = 264
    };

#ifdef WITH_DICT_ENTRY
    enum DictEntryState : byte
    {
        RequireBeginDictEntry = 0,
        InDictEntry,
        RequireEndDictEntry,
        AfterEndDictEntry
    };
#endif
    struct ArrayInfo
    {
        uint32 containedTypeBegin; // to rewind when reading the next element
#ifdef WITH_DICT_ENTRY
        DictEntryState dictEntryState;
        uint32 lengthFieldPosition : 24;
#else
        uint32 lengthFieldPosition;
#endif
    };

    struct VariantInfo
    {
        // a variant switches the currently parsed signature, so we
        // need to store the old signature and parse position.
        uint32 prevSignatureOffset; // relative to m_data
        uint32 prevSignaturePosition;
    };

    struct StructInfo
    {
        uint32 containedTypeBegin;
    };

    struct AggregateInfo
    {
        IoState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
        union {
            ArrayInfo arr;
            VariantInfo var;
            StructInfo sct;
        };
    };

    // this keeps track of which aggregates we are currently in
#ifdef HAVE_BOOST
    boost::container::small_vector<AggregateInfo, 8> m_aggregateStack;
#else
    std::vector<AggregateInfo> m_aggregateStack;
#endif
    std::vector<QueuedDataInfo> m_queuedData;
};

thread_local static MallocCache<sizeof(Arguments::Writer::Private), 4> allocCache;

Arguments::Writer::Private::Private(const Private &other)
{
    *this = other;
}

void Arguments::Writer::Private::operator=(const Private &other)
{
    if (&other == this) {
        assert(false); // if this happens, the (internal) caller did something wrong
        return;
    }

    m_dataElementsCountBeforeNilArray = other.m_dataElementsCountBeforeNilArray;
    m_dataPositionBeforeVariant = other.m_dataPositionBeforeVariant;

    m_nesting = other.m_nesting;
    m_signature.ptr = other.m_signature.ptr; // ### still needs adjustment, done after allocating m_data
    m_signature.length = other.m_signature.length;
    m_signaturePosition = other.m_signaturePosition;

    m_dataCapacity = other.m_dataCapacity;
    m_dataPosition = other.m_dataPosition;
    // handle *m_data and the data it's pointing to
    m_data = reinterpret_cast<byte *>(malloc(m_dataCapacity));
    memcpy(m_data, other.m_data, m_dataPosition);
    m_signature.ptr += m_data - other.m_data;

    m_nilArrayNesting = other.m_nilArrayNesting;
    m_fileDescriptors = other.m_fileDescriptors;
    m_error = other.m_error;

    m_aggregateStack = other.m_aggregateStack;
    m_queuedData = other.m_queuedData;
}

Arguments::Writer::Writer()
   : d(new(allocCache.allocate()) Private),
     m_state(AnyData)
{
}

Arguments::Writer::Writer(Writer &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = nullptr;
}

void Arguments::Writer::operator=(Writer &&other)
{
    if (&other == this) {
        return;
    }
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = nullptr;
}

Arguments::Writer::Writer(const Writer &other)
   : d(nullptr),
     m_state(other.m_state),
     m_u(other.m_u)
{
    if (other.d) {
        d = new(allocCache.allocate()) Private(*other.d);
    }

}

void Arguments::Writer::operator=(const Writer &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    m_u = other.m_u;
    if (d && other.d) {
        *d = *other.d;
    } else {
        Writer temp(other);
        std::swap(d, temp.d);
    }
}

Arguments::Writer::~Writer()
{
    if (d) {
        free(d->m_data);
        d->m_data = nullptr;
        d->~Private();
        allocCache.free(d);
        d = nullptr;
    }
}

bool Arguments::Writer::isValid() const
{
    return !d->m_error.isError();
}

Error Arguments::Writer::error() const
{
    return d->m_error;
}

cstring Arguments::Writer::stateString() const
{
    return printableState(m_state);
}

bool Arguments::Writer::isInsideEmptyArray() const
{
    return d->m_nilArrayNesting > 0;
}

cstring Arguments::Writer::currentSignature() const
{
    // A signature must be null-terminated to be valid.
    // We're only overwriting uninitialized memory, no need to undo that later.
    d->m_signature.ptr[d->m_signature.length] = '\0';
    return d->m_signature;
}

uint32 Arguments::Writer::currentSignaturePosition() const
{
    return d->m_signaturePosition;
}

void Arguments::Writer::doWritePrimitiveType(IoState type, uint32 alignAndSize)
{
    d->reserveData(d->m_dataPosition + (alignAndSize << 1), &m_state);
    zeroPad(d->m_data, alignAndSize, &d->m_dataPosition);

    switch(type) {
    case Boolean: {
        uint32 num = m_u.Boolean ? 1 : 0;
        basic::writeUint32(d->m_data + d->m_dataPosition, num);
        break; }
    case Byte:
        d->m_data[d->m_dataPosition] = m_u.Byte;
        break;
    case Int16:
        basic::writeInt16(d->m_data + d->m_dataPosition, m_u.Int16);
        break;
    case Uint16:
        basic::writeUint16(d->m_data + d->m_dataPosition, m_u.Uint16);
        break;
    case Int32:
        basic::writeInt32(d->m_data + d->m_dataPosition, m_u.Int32);
        break;
    case Uint32:
        basic::writeUint32(d->m_data + d->m_dataPosition, m_u.Uint32);
        break;
    case Int64:
        basic::writeInt64(d->m_data + d->m_dataPosition, m_u.Int64);
        break;
    case Uint64:
        basic::writeUint64(d->m_data + d->m_dataPosition, m_u.Uint64);
        break;
    case Double:
        basic::writeDouble(d->m_data + d->m_dataPosition, m_u.Double);
        break;
    case UnixFd: {
        const uint32 index = d->m_fileDescriptors.size();
        if (!d->m_nilArrayNesting) {
            d->m_fileDescriptors.push_back(m_u.Int32);
        }
        basic::writeUint32(d->m_data + d->m_dataPosition, index);
        break; }
    default:
        assert(false);
        VALID_IF(false, Error::InvalidType);
    }

    d->m_dataPosition += alignAndSize;
    d->maybeQueueData(alignAndSize, alignAndSize);
}

void Arguments::Writer::doWriteString(IoState type, uint32 lengthPrefixSize)
{
    if (type == String) {
        VALID_IF(Arguments::isStringValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidString);
    } else if (type == ObjectPath) {
        VALID_IF(Arguments::isObjectPathValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidObjectPath);
    } else if (type == Signature) {
        VALID_IF(Arguments::isSignatureValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidSignature);
    }

    d->reserveData(d->m_dataPosition + (lengthPrefixSize << 1) + m_u.String.length + 1, &m_state);

    zeroPad(d->m_data, lengthPrefixSize, &d->m_dataPosition);

    if (lengthPrefixSize == 1) {
        d->m_data[d->m_dataPosition] = m_u.String.length;
    } else {
        basic::writeUint32(d->m_data + d->m_dataPosition, m_u.String.length);
    }
    d->m_dataPosition += lengthPrefixSize;
    d->maybeQueueData(lengthPrefixSize, lengthPrefixSize);

    d->appendBulkData(chunk(m_u.String.ptr, m_u.String.length + 1));
}

void Arguments::Writer::advanceState(cstring signatureFragment, IoState newState)
{
    // what needs to happen here:
    // - if we are in an existing portion of the signature (like writing the >1st iteration of an array)
    //   check if the type to be written is the same as the one that's already in the signature
    //   - otherwise we still need to check if the data we're adding conforms with the spec, e.g.
    //     no empty structs, dict entries must have primitive key type and exactly one value type
    // - check well-formedness of data: strings, maximum serialized array length and message length
    //   (variant signature length only being known after finishing a variant introduces uncertainty
    //    of final data stream size - due to alignment padding, a variant signature longer by one can
    //    cause an up to seven bytes longer message. in other cases it won't change message length at all.)
    // - increase size of data buffer when it gets too small
    // - store information about variants and arrays, in order to:
    //   - know what the final binary message size will be
    //   - in finish(), create the final data stream with inline variant signatures and array lengths

    if (unlikely(m_state == InvalidData)) {
        return;
    }
    // can't do the following because a dict is one aggregate in our counting, but two according to
    // the spec: an array (one) containing dict entries (two)
    // assert(d->m_nesting.total() == d->m_aggregateStack.size());
    assert((d->m_nesting.total() == 0) == d->m_aggregateStack.empty());

    m_state = AnyData;
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    if (signatureFragment.length) {
        const TypeInfo ty = typeInfo(signatureFragment.ptr[0]);
        alignment = ty.alignment;
        isPrimitiveType = ty.isPrimitive;
        isStringType = ty.isString;
    }

    bool isWritingSignature = d->m_signaturePosition == d->m_signature.length;
    if (isWritingSignature) {
        // signature additions must conform to syntax
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= MaxSignatureLength,
                 Error::SignatureTooLong);
    }
    if (!d->m_aggregateStack.empty()) {
        Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginVariant:
            // arrays and variants may contain just one single complete type; note that this will
            // trigger only when not inside an aggregate inside the variant or (see below) array
            if (d->m_signaturePosition >= 1) {
                VALID_IF(newState == EndVariant, Error::NotSingleCompleteTypeInVariant);
            }
            break;
        case BeginArray:
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1
                && newState != EndArray) {
                // we are not at start of contained type's signature, the array is at top of stack
                // -> we are at the end of the single complete type inside the array, start the next
                // entry. TODO: check compatibility (essentially what's in the else branch below)
                d->m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
                isWritingSignature = false;
            }
            break;
        case BeginDict:
            if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
#ifdef WITH_DICT_ENTRY
                if (aggregateInfo.arr.dictEntryState == Private::RequireBeginDictEntry) {
                    // This is only reached immediately after beginDict() so it's kinda wasteful, oh well.
                    VALID_IF(newState == BeginDictEntry, Error::MissingBeginDictEntry);
                    aggregateInfo.arr.dictEntryState = Private::InDictEntry;
                    m_state = DictKey;
                    return; // BeginDictEntry writes no data
                }
#endif
                VALID_IF(isPrimitiveType || isStringType, Error::InvalidKeyTypeInDict);
            }
#ifdef WITH_DICT_ENTRY
            // TODO test this part of the state machine
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2) {
                if (aggregateInfo.arr.dictEntryState == Private::RequireEndDictEntry) {
                    VALID_IF(newState == EndDictEntry, Error::MissingEndDictEntry);
                    aggregateInfo.arr.dictEntryState = Private::AfterEndDictEntry;
                    m_state = BeginDictEntry;
                    return; // EndDictEntry writes no data
                } else {
                    // v should've been caught earlier
                    assert(aggregateInfo.arr.dictEntryState == Private::AfterEndDictEntry);
                    VALID_IF(newState == BeginDictEntry || newState == EndDict, Error::MissingBeginDictEntry);
                    // "fall through", the rest (another iteration or finish) is handled below
                }
            } else if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1) {
                assert(aggregateInfo.arr.dictEntryState == Private::InDictEntry);
                aggregateInfo.arr.dictEntryState = Private::RequireEndDictEntry;
                // Setting EndDictEntry after writing a primitive type works fine, but setting it after
                // ending another aggregate would be somewhat involved and need to happen somewhere
                // else, so just don't do that. We still produce an error when endDictEntry() is not
                // used correctly.
                // m_state = EndDictEntry;

                // continue and write the dict entry's value
            }
#endif
            // first type has been checked already, second must be present (checked in EndDict
            // state handler). no third type allowed.
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2
                && newState != EndDict) {
                // align to dict entry
                d->alignData(StructAlignment);
                d->m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
                isWritingSignature = false;
                m_state = DictKey;
#ifdef WITH_DICT_ENTRY
                assert(newState == BeginDictEntry);
                aggregateInfo.arr.dictEntryState = Private::InDictEntry;
                return; // BeginDictEntry writes no data
#endif
            }

            break;
        default:
            break;
        }
    }

    if (isWritingSignature) {
        // extend the signature
        for (uint32 i = 0; i < signatureFragment.length; i++) {
            d->m_signature.ptr[d->m_signaturePosition++] = signatureFragment.ptr[i];
        }
        d->m_signature.length += signatureFragment.length;
    } else {
        // Do not try to prevent several iterations through a nil array. Two reasons:
        // - We may be writing a nil array in the >1st iteration of a non-nil outer array.
        //   This would need to be distinguished from just iterating through a nil array
        //   several times. Which is well possible. We don't bother with that because...
        // - As a QtDBus unittest illustrates, somebody may choose to serialize a fixed length
        //   series of data elements as an array (instead of struct), so that a trivial
        //   serialization of such data just to fill in type information in an outer empty array
        //   would end up iterating through the inner, implicitly empty array several times.
        // All in all it is just not much of a benefit to be strict, so don't.
        //VALID_IF(likely(!d->m_nilArrayNesting), Error::ExtraIterationInEmptyArray);

        // signature must match first iteration (of an array/dict)
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= d->m_signature.length,
                 Error::TypeMismatchInSubsequentArrayIteration);
        // TODO need to apply special checks for state changes with no explicit signature char?
        // (end of array, end of variant)
        for (uint32 i = 0; i < signatureFragment.length; i++) {
            VALID_IF(d->m_signature.ptr[d->m_signaturePosition++] == signatureFragment.ptr[i],
                     Error::TypeMismatchInSubsequentArrayIteration);
        }
    }

    if (isPrimitiveType) {
        doWritePrimitiveType(newState, alignment);
        return;
    }
    if (isStringType) {
        // In case of nil array, skip writing to make sure that the input string (which is explicitly
        // allowed to be garbage) is not validated and no wild pointer is dereferenced.
        if (likely(!d->m_nilArrayNesting)) {
            doWriteString(newState, alignment);
        } else {
            // The alignment of the first element in a nil array determines where array data starts,
            // which is needed to serialize the length correctly. Write the minimum to achieve that.
            // (The check to see if we're really at the first element is omitted - for performance
            // it's worth trying to add that check)
            d->alignData(alignment);
        }
        return;
    }

    Private::AggregateInfo aggregateInfo;

    switch (newState) {
    case BeginStruct:
        VALID_IF(d->m_nesting.beginParen(), Error::ExcessiveNesting);
        aggregateInfo.aggregateType = BeginStruct;
        aggregateInfo.sct.containedTypeBegin = d->m_signaturePosition;
        d->m_aggregateStack.push_back(aggregateInfo);
        d->alignData(alignment);
        break;
    case EndStruct:
        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndStructHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginStruct &&
                 d->m_signaturePosition > aggregateInfo.sct.containedTypeBegin + 1,
                 Error::EmptyStruct); // empty structs are not allowed
        d->m_nesting.endParen();
        d->m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        VALID_IF(d->m_nesting.beginVariant(), Error::ExcessiveNesting);
        aggregateInfo.aggregateType = BeginVariant;

        Private::VariantInfo &variantInfo = aggregateInfo.var;
        variantInfo.prevSignatureOffset = uint32(reinterpret_cast<byte *>(d->m_signature.ptr) - d->m_data);
        d->m_signature.ptr[-1] = byte(d->m_signature.length);
        variantInfo.prevSignaturePosition = d->m_signaturePosition;

        if (!d->insideVariant()) {
            d->m_dataPositionBeforeVariant = d->m_dataPosition;
        }

        d->m_aggregateStack.push_back(aggregateInfo);

        d->m_queuedData.reserve(16);
        d->m_queuedData.push_back(Private::QueuedDataInfo(1, Private::QueuedDataInfo::VariantSignature));

        const uint32 newDataPosition = d->m_dataPosition + Private::SignatureReservedSpace;
        d->reserveData(newDataPosition, &m_state);
        // allocate new signature in the data buffer, reserve one byte for length prefix
        d->m_signature.ptr = reinterpret_cast<char *>(d->m_data) + d->m_dataPosition + 1;
        d->m_signature.length = 0;
        d->m_signaturePosition = 0;
        d->m_dataPosition = newDataPosition;
        break; }
    case EndVariant: {
        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndVariantHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginVariant, Error::CannotEndVariantHere);
        d->m_nesting.endVariant();
        if (likely(!d->m_nilArrayNesting)) {
            // Empty variants are not allowed. As an exception, in nil arrays they are
            // allowed for writing a type signature like "av" in the shortest possible way.
            // No use adding stuff when it's not required or even possible.
            VALID_IF(d->m_signaturePosition > 0, Error::EmptyVariant);
            assert(d->m_signaturePosition <= MaxSignatureLength); // should have been caught earlier
        }
        d->m_signature.ptr[-1] = byte(d->m_signaturePosition);

        Private::VariantInfo &variantInfo = aggregateInfo.var;
        d->m_signature.ptr = reinterpret_cast<char *>(d->m_data) + variantInfo.prevSignatureOffset;
        d->m_signature.length = d->m_signature.ptr[-1];
        d->m_signaturePosition = variantInfo.prevSignaturePosition;
        d->m_aggregateStack.pop_back();

        // if not in any variant anymore, flush queued data and resume unqueued operation
        if (d->m_signature.ptr == reinterpret_cast<char *>(d->m_data) + 1) {
            flushQueuedData();
        }

        break; }

    case BeginDict:
    case BeginArray: {
        VALID_IF(d->m_nesting.beginArray(), Error::ExcessiveNesting);
        if (newState == BeginDict) {
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen(), Error::ExcessiveNesting);
        }

        aggregateInfo.aggregateType = newState;
        aggregateInfo.arr.containedTypeBegin = d->m_signaturePosition;

        d->reserveData(d->m_dataPosition + (sizeof(uint32) << 1), &m_state);
        if (m_state == InvalidData) {
            break; // should be excessive length error from reserveData - do not unset error state
        }
        zeroPad(d->m_data, sizeof(uint32), &d->m_dataPosition);
        basic::writeUint32(d->m_data + d->m_dataPosition, 0);
        aggregateInfo.arr.lengthFieldPosition = d->m_dataPosition;
        d->m_dataPosition += sizeof(uint32);
        d->maybeQueueData(sizeof(uint32), Private::QueuedDataInfo::ArrayLengthField);

        if (newState == BeginDict) {
            d->alignData(StructAlignment);
#ifdef WITH_DICT_ENTRY
            m_state = BeginDictEntry;
            aggregateInfo.arr.dictEntryState = Private::RequireBeginDictEntry;
#else
            m_state = DictKey;
#endif
        }

        d->m_aggregateStack.push_back(aggregateInfo);
        break; }
    case EndDict:
    case EndArray: {
        const bool isDict = newState == EndDict;

        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndArrayHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray),
                 Error::CannotEndArrayOrDictHere);
        VALID_IF(d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + (isDict ? 3 : 1),
                 Error::TooFewTypesInArrayOrDict);
        if (isDict) {
            d->m_nesting.endParen();
        }
        d->m_nesting.endArray();

        // array data starts (and in empty arrays ends) at the first array element position *after alignment*
        const uint32 contentAlign = isDict ? 8
                        : typeInfo(d->m_signature.ptr[aggregateInfo.arr.containedTypeBegin]).alignment;
        const uint32 arrayDataStart = align(aggregateInfo.arr.lengthFieldPosition + sizeof(uint32),
                                            contentAlign);

        if (unlikely(d->m_nilArrayNesting)) {
            if (--d->m_nilArrayNesting == 0) {
                d->m_dataPosition = arrayDataStart;
                if (d->insideVariant()) {
                    assert(d->m_queuedData.begin() + d->m_dataElementsCountBeforeNilArray <=
                           d->m_queuedData.end());
                    d->m_queuedData.erase(d->m_queuedData.begin() + d->m_dataElementsCountBeforeNilArray,
                                          d->m_queuedData.end());
                    assert((d->m_queuedData.end() - 2)->size == Private::QueuedDataInfo::ArrayLengthField);
                    // align, but don't have actual data for the first element
                    d->m_queuedData.back().size = 0;
                }
            }
        }

        // (arrange to) patch in the array length now that it is known
        if (d->insideVariant()) {
            d->m_queuedData.push_back(Private::QueuedDataInfo(1, Private::QueuedDataInfo::ArrayLengthEndMark));
        } else {
            const uint32 arrayLength = d->m_dataPosition - arrayDataStart;
            VALID_IF(arrayLength <= Arguments::MaxArrayLength, Error::ArrayOrDictTooLong);
            basic::writeUint32(d->m_data + aggregateInfo.arr.lengthFieldPosition, arrayLength);
        }
        d->m_aggregateStack.pop_back();
        break; }
#ifdef WITH_DICT_ENTRY
    case BeginDictEntry:
    case EndDictEntry:
        break;
#endif
    default:
        VALID_IF(false, Error::InvalidType);
        break;
    }
}

void Arguments::Writer::beginArrayOrDict(IoState beginWhat, ArrayOption option)
{
    assert(beginWhat == BeginArray || beginWhat == BeginDict);
    if (unlikely(option == RestartEmptyArrayToWriteTypes)) {
        if (!d->m_aggregateStack.empty()) {
            Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
            if (aggregateInfo.aggregateType == beginWhat) {
                // No writes to the array or dict may have occurred yet

                if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
                    // Fix up state as if beginArray/Dict() had been called with WriteTypesOfEmptyArray
                    // in the first place. After that small fixup we're done and return.
                    // The code is a slightly modified version of code below under: if (isEmpty) {
                    if (!d->m_nilArrayNesting) {
                        d->m_nilArrayNesting = 1;
                        d->m_dataElementsCountBeforeNilArray = d->m_queuedData.size() + 2; // +2 as below
                        // Now correct for the elements already added in advanceState() with BeginArray / BeginDict
                        d->m_dataElementsCountBeforeNilArray -= (beginWhat == BeginDict) ? 2 : 1;
                    } else {
                        // The array may be implicitly nil (so our poor API client doesn't notice) because
                        // an array below in the aggregate stack is nil, so just allow this as a no-op.
                    }
                    return;
                }
            }
        }
        VALID_IF(false, Error::InvalidStateToRestartEmptyArray);
    }

    const bool isEmpty = (option != NonEmptyArray) || d->m_nilArrayNesting;
    if (isEmpty) {
        if (!d->m_nilArrayNesting++) {
            // For simplictiy and performance in the fast path, we keep storing the data chunks and any
            // variant signatures written inside an empty array. When we close the array, though, we
            // throw away all that data and signatures and keep only changes in the signature containing
            // the topmost empty array.
            // +2 -> keep ArrayLengthField, and first data element for alignment purposes
            d->m_dataElementsCountBeforeNilArray = d->m_queuedData.size() + 2;
        }
    }
    if (beginWhat == BeginArray) {
        advanceState(cstring("a", strlen("a")), beginWhat);
    } else {
        advanceState(cstring("a{", strlen("a{")), beginWhat);
    }
}

void Arguments::Writer::beginArray(ArrayOption option)
{
    beginArrayOrDict(BeginArray, option);
}

void Arguments::Writer::endArray()
{
    advanceState(cstring(), EndArray);
}

void Arguments::Writer::beginDict(ArrayOption option)
{
    beginArrayOrDict(BeginDict, option);
}

void Arguments::Writer::endDict()
{
    advanceState(cstring("}", strlen("}")), EndDict);
}

#ifdef WITH_DICT_ENTRY
void Arguments::Writer::beginDictEntry()
{
    VALID_IF(m_state == BeginDictEntry, Error::MisplacedBeginDictEntry);
    advanceState(cstring(), BeginDictEntry);
}

void Arguments::Writer::endDictEntry()
{
    if (!d->m_aggregateStack.empty()) {
        Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        if (aggregateInfo.aggregateType == BeginDict
            && aggregateInfo.arr.dictEntryState == Private::RequireEndDictEntry) {
            advanceState(cstring(), EndDictEntry);
            return;
        }
    }
    VALID_IF(false, Error::MisplacedEndDictEntry);
}
#endif

void Arguments::Writer::beginStruct()
{
    advanceState(cstring("(", strlen("(")), BeginStruct);
}

void Arguments::Writer::endStruct()
{
    advanceState(cstring(")", strlen(")")), EndStruct);
}

void Arguments::Writer::beginVariant()
{
    advanceState(cstring("v", strlen("v")), BeginVariant);
}

void Arguments::Writer::endVariant()
{
    advanceState(cstring(), EndVariant);
}

void Arguments::Writer::writeVariantForMessageHeader(char sig)
{
    // Note: the sugnature we're vorking with there is a(yv)
    // If we know that and can trust the client, this can be very easy and fast...
    d->m_signature.ptr[3] = 'v';
    d->m_signature.length = 4;
    d->m_signaturePosition = 4;

    d->reserveData(d->m_dataPosition + 3, &m_state);
    d->m_data[d->m_dataPosition++] = 1;
    d->m_data[d->m_dataPosition++] = sig;
    d->m_data[d->m_dataPosition++] = 0;
}

void Arguments::Writer::fixupAfterWriteVariantForMessageHeader()
{
    // We just wrote something to the main signature when we shouldn't have.
    d->m_signature.length = 4;
    d->m_signaturePosition = 4;
}

static char letterForPrimitiveIoState(Arguments::IoState ios)
{
    if (ios < Arguments::Boolean || ios > Arguments::Double) {
        return  'c'; // a known invalid letter that won't trip up typeInfo()
    }
    static const char letters[] = {
        'b', // Boolean
        'y', // Byte
        'n', // Int16
        'q', // Uint16
        'i', // Int32
        'u', // Uint32
        'x', // Int64
        't', // Uint64
        'd'  // Double
    };
    return letters[size_t(ios) - size_t(Arguments::Boolean)]; // TODO do we need the casts?
}

void Arguments::Writer::writePrimitiveArray(IoState type, chunk data)
{
    const char letterCode = letterForPrimitiveIoState(type);
    if (letterCode == 'c') {
        m_state = InvalidData;
        d->m_error.setCode(Error::NotPrimitiveType);
        return;
    }
    if (data.length > Arguments::MaxArrayLength) {
        m_state = InvalidData;
        d->m_error.setCode(Error::ArrayOrDictTooLong);
        return;
    }

    const TypeInfo elementType = typeInfo(letterCode);
    if (!isAligned(data.length, elementType.alignment)) {
        m_state = InvalidData;
        d->m_error.setCode(Error::CannotEndArrayOrDictHere);
        return;
    }

    beginArray(data.length ? NonEmptyArray : WriteTypesOfEmptyArray);

    // dummy write to write the signature...
    m_u.Uint64 = 0;
    advanceState(cstring(&letterCode, /*length*/ 1), elementType.state());

    if (!data.length) {
        // oh! a nil array (which is valid)
        endArray();
        return;
    }

    // undo the dummy write (except for the preceding alignment bytes, if any)
    d->m_dataPosition -= elementType.alignment;
    if (d->insideVariant()) {
        d->m_queuedData.pop_back();
        d->m_queuedData.push_back(Private::QueuedDataInfo(elementType.alignment, 0));
    }

    // append the payload
    d->reserveData(d->m_dataPosition + data.length, &m_state);
    d->appendBulkData(data);

    endArray();
}

Arguments Arguments::Writer::finish()
{
    // what needs to happen here:
    // - check if the message can be closed - basically the aggregate stack must be empty
    // - close the signature by adding the terminating null

    Arguments args;

    if (m_state == InvalidData) {
        args.d->m_error = d->m_error;
        return args; // heavily relying on NRVO in all returns here!
    }
    if (d->m_nesting.total() != 0) {
        m_state = InvalidData;
        d->m_error.setCode(Error::CannotEndArgumentsHere);
        args.d->m_error = d->m_error;
        return args;
    }
    assert(!d->m_nilArrayNesting);
    assert(!d->insideVariant());

    assert(d->m_signaturePosition <= MaxSignatureLength); // this should have been caught before
    assert(d->m_signature.ptr == reinterpret_cast<char *>(d->m_data) + 1);

    // Note that we still keep the full SignatureReservedSpace for the main signature, which means
    // less copying around to shrink the gap between signature and data, but also wastes an enormous
    // amount of space (relative to the possible minimum) in some cases. It should not be a big space
    // problem because normally not many D-Bus Message / Arguments instances exist at the same time.

    d->m_signature.length = d->m_signaturePosition;
    d->m_signature.ptr[d->m_signature.length] = '\0';

    // OK, so this length check is more of a sanity check. The actual limit limits the size of the
    // full message. Here we take the size of the "payload" and don't add the size of the signature -
    // why bother doing it accurately when the real check with full information comes later anyway?
    bool success = true;
    const uint32 dataSize = d->m_dataPosition - Private::SignatureReservedSpace;
    if (success && dataSize > Arguments::MaxMessageLength) {
        success = false;
        d->m_error.setCode(Error::ArgumentsTooLong);
    }

    if (!dataSize || !success) {
        args.d->m_memOwnership = nullptr;
        args.d->m_signature = cstring();
        args.d->m_data = chunk();
    } else {
        args.d->m_memOwnership = d->m_data;
        args.d->m_signature = cstring(d->m_data + 1 /* w/o length prefix */, d->m_signature.length);
        args.d->m_data = chunk(d->m_data + Private::SignatureReservedSpace, dataSize);
        d->m_data = nullptr; // now owned by Arguments and later freed there
    }

    if (success) {
        args.d->m_fileDescriptors = std::move(d->m_fileDescriptors);
        m_state = Finished;
    } else {
        m_state = InvalidData;
        args.d->m_error = d->m_error;
    }
    return args;
}

struct ArrayLengthField
{
    uint32 lengthFieldPosition;
    uint32 dataStartPosition;
};

void Arguments::Writer::flushQueuedData()
{
    const uint32 count = d->m_queuedData.size();
    assert(count); // just don't call this method otherwise!

    // Note: if one of signature or data is nonempty, the other must also be nonempty.
    // Even "empty" things like empty arrays or null strings have a size field, in that case
    // (for all(?) types) of value zero.

    // Copy the signature and main data (thus the whole contents) into one allocated block,
    // which is good to have for performance and simplicity reasons.

    // The maximum alignment blowup for naturally aligned types is just less than a factor of 2.
    // Structs and dict entries are always 8 byte aligned so they add a maximum blowup of 7 bytes
    // each (when they contain a byte).
    // Those estimates are very conservative (but easy!), so some space optimization is possible.

    uint32 inPos = d->m_dataPositionBeforeVariant;
    uint32 outPos = d->m_dataPositionBeforeVariant;
    byte *const buffer = d->m_data;

    std::vector<ArrayLengthField> lengthFieldStack;

    for (uint32 i = 0; i < count; i++) {
        const Private::QueuedDataInfo ei = d->m_queuedData[i];
        switch (ei.size) {
        case 0: {
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
            }
            break;
        default: {
                assert(ei.size && ei.size <= Private::QueuedDataInfo::LargestSize);
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
                // copy data chunk
                memmove(buffer + outPos, buffer + inPos, ei.size);
                inPos += ei.size;
                outPos += ei.size;
            }
            break;
        case Private::QueuedDataInfo::ArrayLengthField: {
                //   start of an array
                // alignment padding before length field
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
                // reserve length field
                ArrayLengthField al;
                al.lengthFieldPosition = outPos;
                inPos += sizeof(uint32);
                outPos += sizeof(uint32);
                // alignment padding before first array element
                assert(i + 1 < d->m_queuedData.size());
                const uint32 contentsAlignment = d->m_queuedData[i + 1].alignment();
                inPos = align(inPos, contentsAlignment);
                zeroPad(buffer, contentsAlignment, &outPos);
                // array data starts at the first array element position after alignment
                al.dataStartPosition = outPos;
                lengthFieldStack.push_back(al);
            }
            break;
        case Private::QueuedDataInfo::ArrayLengthEndMark: {
                //   end of an array
                // just put the now known array length in front of the array
                const ArrayLengthField al = lengthFieldStack.back();
                const uint32 arrayLength = outPos - al.dataStartPosition;
                if (arrayLength > Arguments::MaxArrayLength) {
                    m_state = InvalidData;
                    d->m_error.setCode(Error::ArrayOrDictTooLong);
                    i = count + 1; // break out of the loop
                    break;
                }
                basic::writeUint32(buffer + al.lengthFieldPosition, arrayLength);
                lengthFieldStack.pop_back();
            }
            break;
        case Private::QueuedDataInfo::VariantSignature: {
                // move the signature and add its null terminator
                const uint32 length = buffer[inPos] + 1; // + length prefix
                memmove(buffer + outPos, buffer + inPos, length);
                buffer[outPos + length] = '\0';
                outPos += length + 1; // + null terminator
                inPos += Private::Private::SignatureReservedSpace;
            }
            break;
        }
    }
    assert(m_state == InvalidData || lengthFieldStack.empty());

    d->m_dataPosition = outPos;
    d->m_queuedData.clear();
}

std::vector<Arguments::IoState> Arguments::Writer::aggregateStack() const
{
    std::vector<IoState> ret;
    ret.reserve(d->m_aggregateStack.size());
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        ret.push_back(aggregate.aggregateType);
    }
    return ret;
}

uint32 Arguments::Writer::aggregateDepth() const
{
    return d->m_aggregateStack.size();
}

Arguments::IoState Arguments::Writer::currentAggregate() const
{
    if (d->m_aggregateStack.empty()) {
        return NotStarted;
    }
    return d->m_aggregateStack.back().aggregateType;
}

chunk Arguments::Writer::peekSerializedData() const
{
    chunk ret;
    if (isValid() && m_state != InvalidData && d->m_nesting.total() == 0) {
        ret.ptr = d->m_data + Private::SignatureReservedSpace;
        ret.length = d->m_dataPosition - Private::SignatureReservedSpace;
    }
    return ret;
}

const std::vector<int> &Arguments::Writer::fileDescriptors() const
{
    return d->m_fileDescriptors;
}

void Arguments::Writer::writeBoolean(bool b)
{
    m_u.Boolean = b;
    advanceState(cstring("b", strlen("b")), Boolean);
}

void Arguments::Writer::writeByte(byte b)
{
    m_u.Byte = b;
    advanceState(cstring("y", strlen("y")), Byte);
}

void Arguments::Writer::writeInt16(int16 i)
{
    m_u.Int16 = i;
    advanceState(cstring("n", strlen("n")), Int16);
}

void Arguments::Writer::writeUint16(uint16 i)
{
    m_u.Uint16 = i;
    advanceState(cstring("q", strlen("q")), Uint16);
}

void Arguments::Writer::writeInt32(int32 i)
{
    m_u.Int32 = i;
    advanceState(cstring("i", strlen("i")), Int32);
}

void Arguments::Writer::writeUint32(uint32 i)
{
    m_u.Uint32 = i;
    advanceState(cstring("u", strlen("u")), Uint32);
}

void Arguments::Writer::writeInt64(int64 i)
{
    m_u.Int64 = i;
    advanceState(cstring("x", strlen("x")), Int64);
}

void Arguments::Writer::writeUint64(uint64 i)
{
    m_u.Uint64 = i;
    advanceState(cstring("t", strlen("t")), Uint64);
}

void Arguments::Writer::writeDouble(double d)
{
    m_u.Double = d;
    advanceState(cstring("d", strlen("d")), Double);
}

void Arguments::Writer::writeString(cstring string)
{
    m_u.String.ptr = string.ptr;
    m_u.String.length = string.length;
    advanceState(cstring("s", strlen("s")), String);
}

void Arguments::Writer::writeObjectPath(cstring objectPath)
{
    m_u.String.ptr = objectPath.ptr;
    m_u.String.length = objectPath.length;
    advanceState(cstring("o", strlen("o")), ObjectPath);
}

void Arguments::Writer::writeSignature(cstring signature)
{
    m_u.String.ptr = signature.ptr;
    m_u.String.length = signature.length;
    advanceState(cstring("g", strlen("g")), Signature);
}

void Arguments::Writer::writeUnixFd(int32 fd)
{
    m_u.Int32 = fd;
    advanceState(cstring("h", strlen("h")), UnixFd);
}
