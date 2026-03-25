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

#include "message.h"
#include "fercode_p.h"
#include "types.h"

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

#include <iostream> // TODO remove

#undef VALID_IF
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    assert(false); m_state = InvalidData; /* TODO handle errCode */ return s_nullBuffer; }


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

    std::vector<FerCode> prevOps;
    uint32 prevOpsIndex;
};

class Arguments::BcReader::Private
{
public:
    const FerCode *m_opsPtr{};    // end pointer not needed, we stop at FerOpcode::End
    const byte *m_dataPtr{};
    const byte *m_dataEnd{};
    std::vector<FerCode> m_ops;
    chunk m_data; // TODO possibly replace, only operate on m_dataPtr and m_dataEnd, possibly use 32-bit pointer diffs
                  // in array operations to save space (should be fine because all in same data array with limited length)
                  // ... or just store a pointer to args and use its data in the rare cases that we need it
    Nesting m_nesting;
    // this keeps track of the limits of the current array
#ifdef HAVE_BOOST
    boost::container::small_vector<uint32, 8> m_arrayLengthStack;
#else
    std::vector<uint32> m_arrayLengthStack;
#endif
    std::vector<VariantInfo> m_variantStack;
};

static const char s_nullBuffer[16] {}; // inert fake data for callers when reading bad or nonexistent data


// TODO must memoize signature -> FerCode to amortize its generation and get a real performance benefit
Arguments::BcReader::BcReader(const Arguments &args)
    : d(new Private)
{
    d->m_ops = ferCodeForSignature(args.signature());
    d->m_data = args.d->m_data;
    beginRead();
}

Arguments::BcReader::BcReader(const Message &msg)
    : d(new Private)
{
    const Arguments &args = msg.arguments();
    d->m_ops = ferCodeForSignature(args.signature());
    d->m_data = args.d->m_data;
    beginRead();
}

Arguments::BcReader::BcReader(BcReader &&other)
    : m_state(other.m_state),
      d(other.d)
{
    other.d = nullptr;
}

void Arguments::BcReader::operator=(BcReader &&other)
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

Arguments::BcReader::BcReader(const BcReader &other)
    : m_state(other.m_state),
      d(nullptr)

{
    if (other.d) {
        d = new Private(*other.d);
    }
}

void Arguments::BcReader::operator=(const BcReader &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    if (d && other.d) {
        *d = *other.d;
    } else {
        BcReader temp(other);
        std::swap(d, temp.d);
    }
}

Arguments::BcReader::~BcReader()
{
    delete d;
    d = nullptr;
}

bool Arguments::BcReader::isValid() const
{
    return true; // TODO
}

Error Arguments::BcReader::error() const
{
    return Error::NoError; // TODO
}

bool Arguments::BcReader::beginArray(EmptyArrayOption option)
{
    (void)option;
    return true; // TODO
}

bool Arguments::BcReader::beginDict(EmptyArrayOption option)
{
    (void)option;
    return true; // TODO
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
    (void)lengthPrefixSize;
}

void Arguments::BcReader::beginRead()
{
    std::cout << "BcBegin " << printableFerOps(d->m_ops) << '\n';

    d->m_opsPtr = &d->m_ops[1];
    m_state = d->m_opsPtr->op.ioState;

    d->m_dataPtr = d->m_data.ptr;
    d->m_dataEnd = d->m_dataPtr + d->m_data.length;
}

// TODO what is actually needed in a FerCode...
// - ioState for next element
// - "read"/"skip" (size) for this element
// - align for next element
// what we currently have:
// - ioState for *this* element
// - "read"/"skip" (size) for this element
// - align for next element

const void *Arguments::BcReader::advanceState()
{
    if (unlikely(m_state == InvalidData)) { // nonrecoverable...
        return s_nullBuffer;
    }

    // Normally (in release builds), no need for a length check on ops because ops are pre-validated and
    // FerOps::End marks the end.
    assert(d->m_opsPtr <= &d->m_ops[0] + d->m_ops.size());
    assert(d->m_dataPtr <= d->m_dataEnd);

    FerOp ferOp = d->m_opsPtr->op;
    std::cout << "BcAdvance opsIdx: " << uint64(d->m_opsPtr - &d->m_ops[0])
              << ", op: " << ferOp.op
              << ", dataIdx: " << uint64(d->m_dataPtr - d->m_data.ptr)
              << '\n';
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
        // TODO validate: utf8, len, trailing nul;
        // maybe do content validation after alignment to next element and data length check to avoid
        // length-checking twice. But we do need to check max string length, for String only though!
        uint32 len = *reinterpret_cast<const uint32 *>(ret);
        newPtr += sizeof(uint32);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case FerOpcode::ObjectPath: {
        // TODO validate: utf8, len, trailing nul, valid object path
        byte len = *reinterpret_cast<const byte *>(ret);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;
        break; }
    case FerOpcode::Signature: {
        // TODO validate: utf8, len, trailing nul, valid signature
        byte len = *reinterpret_cast<const byte *>(ret);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;
        break; }

    case FerOpcode::BeginArray: {
        const uint32 arrayLength = *reinterpret_cast<const uint32 *>(d->m_dataPtr);
        const byte *endPtr = d->m_dataPtr + sizeof(uint32) + arrayLength;
        if (endPtr > d->m_dataEnd || arrayLength > Arguments::MaxArrayLength) {
            // TODO error
        }
        d->m_arrayLengthStack.push_back(d->m_data.length);
        // Array length becomes (until end of array) our new "data entire" length, reuses the same check
        d->m_dataEnd = endPtr;
        // TODO what to return from this function here? Maybe number of elements if fixed size / cheap to determine?
        break; }

    case FerOpcode::EndArray: {
        // TODO??? the cursed nil array thing... maybe remove some conditionals by supplying an 8 byte buffer
        // of nulls to read data from and always reset to its beginning after every read.

        // TODO restore d->m_dataEnd if finished

        const FerEndArray endArrayData = d->m_opsPtr->endArray;
        if (d->m_dataPtr < d->m_dataEnd) {
            // end
            // NB: ferPack.op.postAlignExponent is already meant for the next array element in this case
            d->m_opsPtr = &d->m_ops[0] + endArrayData.repeatArrayIndex;
        } else if (d->m_dataPtr == d->m_dataEnd) {
            d->m_data.length = d->m_arrayLengthStack.back();
            d->m_arrayLengthStack.pop_back();
            ferOp.postAlignExponent = endArrayData.afterArrayAlignExponent;
            d->m_opsPtr++;
        } else {
            assert(false); // for now, TODO error handling
        }
        break; }

    case FerOpcode::EnterVariant: {
        const FerNesting outerNest = d->m_opsPtr->nest;
        d->m_nesting.array += outerNest.arrayDepth;
        d->m_nesting.paren += outerNest.parenDepth;
        d->m_nesting.variant += 1;

        const uint32 opsIndex = d->m_opsPtr + 1 - &d->m_ops[0]; // point it after the EnterVariant + FerNest
        d->m_variantStack.push_back(VariantInfo{std::move(d->m_ops), opsIndex}); // TODO? emplace_back
        d->m_ops.clear();

        const byte len = *reinterpret_cast<const byte *>(ret);
        const cstring newSig(const_cast<byte *>(ret + sizeof(byte)), len);
        newPtr += sizeof(byte);
        newPtr += len + 1 /* trailing nul */;

        d->m_ops = ferCodeForSignature(newSig, Arguments::VariantSignature);
        assert(d->m_ops.size() >= 4); // two begin, one end, >= 1 data elements  // TODO error handling
        d->m_opsPtr = &d->m_ops[0];

        const FerBeginVariantSpecial variantSpecial = d->m_opsPtr->beginVariantSpecial;
        assert(variantSpecial.op == FerOpcode::BeginVariantSignature);
        const FerNesting innerNest = (d->m_opsPtr + 1)->nest;
        d->m_opsPtr += 2;
        if (d->m_nesting.array + innerNest.arrayDepth > Nesting::arrayMax ||
            d->m_nesting.paren + innerNest.parenDepth > Nesting::parenMax ||
            d->m_nesting.total() + variantSpecial.combinedDepth > Nesting::totalMax) {
            assert(false); // for now, TODO error handling
        }

        ferOp.postAlignExponent = variantSpecial.postAlignExponent;
        // avoid bit fiddling by copying all bits in the first byte of variantSpecial - it makes no other
        // difference; only postAlignExponent and ioState will be used in the remainder of this function
        ferOp.op = variantSpecial.op;

        break; }

    case FerOpcode::EndVariant: {
        assert(ferOp.postAlignExponent == 0); // alignment for next element at the end never makes sense

        VariantInfo& varInfo = d->m_variantStack.back();
        d->m_ops = std::move(varInfo.prevOps);
        d->m_opsPtr = &d->m_ops[0] + varInfo.prevOpsIndex;
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
    case BeginVariantSignature:
    case BeginMethodSignature:
        // These two should only occur at the beginning of a signature, which we don't handle here
        unreachable();
        break;
    }


    const byte *unalignedNewPtr;
    switch(ferOp.postAlignExponent) {
    case 0:
        // ### fast path, not sure if this makes sense...
        VALID_IF(d->m_dataPtr <= d->m_dataEnd, TODOError);
        m_state = ferOp.ioState;
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

    VALID_IF(newPtr <= d->m_dataEnd, TODOError);

    VALID_IF(isPaddingZero(unalignedNewPtr, newPtr), TODOerror);

    m_state = ferOp.ioState;
    d->m_dataPtr = newPtr;
    return ret;
}
