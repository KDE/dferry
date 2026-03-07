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

#include "arguments.h"
#include "arguments_p.h"

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

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

struct BeginVariantData
{
    byte arrayDepth;
    byte structDepth;
};

struct EndArrayData
{
    byte afterArrayAlignExponent : 2;         // for the "end of array" case
    uint16 repeatArrayIndex : 14;
};

enum PackOp : byte
{
    Copy0 = 0,// No-op
    Copy1,
    Copy2,
    Copy4,
    Copy8,
    String,
    ObjectPath,
    Signature,
    BeginArray,
    EndArray,       // alignment in this case is for the "go back" case, alignment for the "end of array"
                    // case is in EndArrayData
    BeginVariant,
    End,
};

struct NormalPackOp
{
    byte postAlignExponent : 2; // this is for the *following* element!
    PackOp op : 6;
    Arguments::IoState ioState;
};

union FerPack
{
    NormalPackOp op;
    EndArrayData endArrayData; // always follows a NormalPackOp with BeginArray
    BeginVariantData beginVariantData; // always follows a NormalPackOp with BeginVariant
};


class Arguments::BcReader::Private
{
public:
    std::vector<FerPack> m_ops;
    FerPack* m_opsPtr{};
    chunk m_data;
    uint32 m_dataPosition{};
    // this keeps track of the limits of the current array
#ifdef HAVE_BOOST
    boost::container::small_vector<uint32, 8> m_arrayLengthStack;
#else
    std::vector<uint32> m_arrayLengthStack;
#endif
};

static const char s_nullBuffer[16]; // for "data" while reading types of empty array


// TODO must memoize signature -> FerCode to amortize its generation and get a real performance benefit
Arguments::BcReader::BcReader(const Arguments &args)
    : d(new Private)
{
    d->m_ops = ferOpsForSignature(args.signature(), false);
}

Arguments::BcReader::BcReader(const Message &msg)
    : d(new Private)
{
    d->m_ops = ferOpsForSignature(msg.arguments().signature(), false);
}

Arguments::BcReader::BcReader(BcReader &&other)
{
}

void Arguments::BcReader::operator=(BcReader &&other)
{
}

Arguments::BcReader::BcReader(const BcReader &other)
{
}

void Arguments::BcReader::operator=(const BcReader &other)
{
}

Arguments::BcReader::~BcReader()
{
    delete d;
    d = nullptr;
}

bool Arguments::BcReader::isValid() const
{
}

Error Arguments::BcReader::error() const
{
}

bool Arguments::BcReader::beginArray(EmptyArrayOption option)
{
}

bool Arguments::BcReader::beginDict(EmptyArrayOption option)
{
}

void Arguments::BcReader::endDict()
{
}

void Arguments::BcReader::beginStruct()
{
}

void Arguments::BcReader::endStruct()
{
}

void Arguments::BcReader::beginVariant()
{
}

void Arguments::BcReader::endVariant()
{
}

#ifdef WITH_DICT_ENTRY
void Arguments::BcReader::beginDictEntry()
{
}

void Arguments::BcReader::endDictEntry()
{
}
#endif

void Arguments::BcReader::doReadString(uint32 lengthPrefixSize)
{
}

void Arguments::BcReader::beginRead()
{
    d->m_dataEnd = d->m_dataPtr + d->m_dataLength;
    d->m_opsPtr = &d->m_ops[0];
}

void *Arguments::BcReader::advanceState()
{
    // Preconditions:
    // - ops pointer points to previous data item's IoState, like Uint32, String etc
    // - data pointer points to previous data item's data, in case of a string that's after the length field
    //   (maybe change this and readString() also to match)
    // - (Would it work better to just copy out the data to not repeat stuff like string parsing? Probably?)
    // - (TODO some wrinkles for arrays?)
    // - (TODO some wrinkles for variants?)
    // Operations:
    // - Payload length check for scalars
    //   - May be able to partially optimize by "summarizing" scalar data in pre-parsing
    //     - Anything beneficial possible with var length data?
    //   - Can this be merged with array length checks? Probably?
    // - Align if necessary
    // - Advance ops pointer to "current" data item's IoState
    // - Advance data pointer "current" data item's, well, data
    // - Array end: length check etc
    //   - Dict vs array checks? (may be nice to avoid "DictEntry" stuff)
    //     - But maybe/probably also support explicit DictEntry if so desired
    //   - Ends exactly after payload, note without alignment to next item? -> normal end
    //   - Ends with bytes left but they don't fit another element? -> error
    //   - Ends with bytes left and they do fit another element? -> another iteration
    //     - Pre-parsing may be able to help optimize again...
    //   - Variant?
    //     - "Hydrate" regular Reader state, for now...
    //     - Stay in BcReader?
    //       - Get a cached ops vector for signature or create and add to cache...
    //       - Patch up stuff to make it work
    //     - If there are both options, what's the API for that? Maybe...
    //       - beginBcVariant() + beginDynVarian()? Eh... not sure.
    // - Allow to disable all(!) sanity checks for more speed?
    // Postcondition: Same, because advanceState() is called again afterwards...
    // (TODO items to make it work?)
    // - Add maybe some header and/or trailer to FerOps to make it work...
    // - Add something to distinguish a(kv) from a{kv} (array of structs vs "dictionary type")

    // Code generation...
    // - Probably "copy" ops vector generation to Python code, optimizations will also help code,
    //   possibly more than for bytecode (see next point)
    // - Maybe more opportunities to merge and summarize stuff (to eliminate work) than in interpreted ops...
    //   e.g. just memcpy series of scalars maybe after cheking padding is zero; here, the "long copy"
    //   operations become useful... (a little extra work for Copy>128 due to granularity limitations)
    // - Generate structs
    // - Generate code
    // - For scalars, use pointers or copy?
    // - Extension to XML format to get proper field names? Otherwise we'd need field0, field1 etc
    // - For strings, pretty sure that pointers into input buffer are better than memcpying the whole thing

    // if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData
    // we don't have to deal with invalid signatures here because they are checked beforehand EXCEPT
    // for aggregate nesting which cannot be checked using only one signature, due to variants.
    // variant signatures are only parsed while reading the data. individual variant signatures
    // ARE checked beforehand whenever we find one in this method.

#if 0
    if (unlikely(m_state == InvalidData)) { // nonrecoverable...
        return;
    }
#endif

parseMore:
    // Normally (in release builds), no need for a length check on ops because ops are pre-validated and
    // FerOps::End marks the end.
    assert(d->m_opsPtr <= &d->m_ops[0] + d->m_ops.size());

    FerPack ferPack = *d->m_opsPtr;
    d->m_opsPtr++;

    byte* ret = d->m_data.ptr + d->m_dataPosition;
    byte* newPtr = ret;


    switch (ferPack.op.op) {
    case PackOp::Copy0:
        // BeginStruct / EndStruct would be such a case. Alignment was in the previous FerPack and nothing
        // else really happens here, but we still need a new IoState "BeginStruct" for the client.
        // ### Maybe "return ret;" directly?
        break;
    case PackOp::Copy1:
        newPtr += 1;
        break;
    case PackOp::Copy2:
        newPtr += 2;
        break;
    case PackOp::Copy4:
        newPtr += 4;
        break;
    case PackOp::Copy8:
        newPtr += 8;
        break;
    case PackOp::String: {
        // TODO validate: utf8, len, trailing nul;
        // maybe do content validation after alignment to next element and data length check to avoid
        // length-checking twice. But we do need to check max string length, for String only though!
        uint32 len = *reinterpret_cast<uint32 *>(ret);
        newPtr += sizeof(uint32);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case PackOp::ObjectPath: {
        // TODO validate: utf8, len, trailing nul, valid object path
        byte len = *reinterpret_cast<byte *>(ret);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case PackOp::Signature: {
        // TODO validate: utf8, len, trailing nul, valid signature
        byte len = *reinterpret_cast<byte *>(ret);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case PackOp::BeginArray:
        // TODO validate length? Maybe get length and return it?
        break;
    case PackOp::EndArray: {
        // TODO??? the cursed nil array thing... maybe remove some conditionals by supplying an 8 byte buffer
        // of nulls to read data from and always reset to its beginning after every read.
        const FerPack arrayPack = *d->m_opsPtr;
        if (moreElements) { // TODO check another go-around
            // ### m_ops might not work inside a variant, depending on how exactly we handle them regarding ops.
            // It might be slightly faster anyway to encode the go back index as a negative offset instead of an
            // absolute position. Could be done while translating FerOps to FerPacks.

            // NB: ferPack.op.postAlignExponent is meant for the next array element in this case!
            d->m_opsPtr = &d->m_ops[0] + arrayPack.endArrayData.repeatArrayIndex;
        } else {
            ferPack.op.postAlignExponent = arrayPack.endArrayData.afterArrayAlignExponent;
            d->m_opsPtr++;
        }
        break; }
    case PackOp::BeginVariant:
        break;
    case PackOp::End:
        // TODO handle "EndVariant" here as well so that there is no need to distinguish variant signatures from
        // function signatures for signature -> FerOps / PackOps caching purposes. We might still need to do that
        // for validation skipping purposes (signature is cached -> it's validated), but one entry could otherwise
        // still serve both.
        assert(ferPack.op.postAlignExponent == 0); // alignment for next element at the end never makes sense
        break;
    }

    switch(ferPack.op.postAlignExponent) {
    case 0:
        break;
    case 1:
        // TODO align2 on newPtr
        newPtr = align(newPtr, 2);
        break;
    case 2:
        newPtr = align(newPtr, 4);
        break;
    case 3:
        newPtr = align(newPtr, 8);
        break;
    }

    // TODO length check goes here (don't do one before alignment, could be redundant. Yeah we might not read one
    // good element before erroring out, but length errors are a "should never happen" situation anyway)
    // ATTENTION: make sure that the last FerPack *never* contains a nonzero alignment, or we'd check garbage.
    // Alignment after the last message byte makes no sense anyway.

    VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition), Error::MalformedMessageData);


    d->m_dataPosition = newPtr;


    return ret;

out_needMoreData:
    // TODO

    return ret;
}

