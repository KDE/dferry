// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef FERCODE_P_H
#define FERCODE_P_H

#include "arguments.h"
#include "argumentsbcreader.h"

#include <cassert>

enum FerOpcode : byte
{
    Copy1 = 0,  // This arrangement of CopyN values allows the expression 1 << uint32(CopyN)
    Copy2,      // to determine the copy size
    Copy4,
    Copy8,
    Copy0,      // No-op (for e.g. IoState BeginStruct and EndStruct, which only align and change state)
    String,
    ObjectPath,
    Signature,
    BeginArray,     // TODO? BeginFixedArray where element count can be trivially calculated from length
    EndArray,       // Alignment and ioState are for the "end array" case (i.e. field after the array)
                    // Always followed by a FerRepeatArray.
    EnterVariant,   // Always followed by a FerNesting giving the nesting depth where the variant
                    // occurs, so current nesting + variant's nesting can be checked against limits

    BeginVariantSignature,  // Only occurs as first (index 0) field of FerCode vector.
                            // Single complete type / Always followed by two FerNesting /
                            // assumes data starting from an unaligned address
    EndVariantSignature,

    BeginMethodSignature,   // Only occurs as first (index 0) field of FerCode vector.
                            // Never followed by FerNesting / assumes starting from an 8-byte aligned address
    End,
};

struct alignas(4) FerOp
{
    FerOpcode op;
    byte postAlign; // this is for the *following* element!
    Arguments::IoState ioState;
    byte arrayNotes{};

    // For BeginArray opcodes, arrayNotes and bits 7..4 of postAlign encode the index of the corresponding
    // EndArray op, for fast skipping. Useful (faster) in ArgumentsBcReader::beginArray() for empty arrays
    // and in ArgumentsBcWriter::endArray() for all arrays.
    // For all other opcodes, arrayNotes != 0 indicates that this opcode is the last inside an array (so
    // looping back is in order) and postAlign will not have any additional data stuffed in.

    bool isLastOfArray() const
    {
        assert(op != FerOpcode::BeginArray);
        return arrayNotes != 0;
    }

    void setCleanPostAlignment(uint32 align)
    {
        assert((align & 0xfffffff0) == 0);
        postAlign &= 0xf0;
        postAlign |= align;
    }

    uint32 cleanPostAlignment() const
    {
        return postAlign & 0x0f;
    }

    void setArrayEndIndex(uint32 endIndex)
    {
        assert(op == FerOpcode::BeginArray);
        assert(endIndex <= 0xfff);
        assert((postAlign & 0xf0) == 0);
        if (endIndex > 0xfff) {
            unreachable();
        }
        arrayNotes =  endIndex & 0x0ff;
        postAlign |= (endIndex & 0xf00) >> 4;
    }

    uint32 arrayEndIndex() const
    {
        assert(op == FerOpcode::BeginArray);
        return arrayNotes + ((postAlign & 0xf0) << 4);
    }
};

struct alignas(4) FerRepeatArray
{
    byte goBackAlign;
    byte padding = 0;
    uint16 goBackOpIndex;
};

struct alignas(4) FerNesting
{
    byte arrayDepth;
    byte parenDepth;
    // TODO add the other depth thing and use it for variant signatures
    uint16 padding = 0;
};

union alignas(4) FerCode
{
    FerCode(FerOp o) : op{o} {}
    FerCode(FerRepeatArray a) : repeatArray{a} {}
    FerCode(FerNesting n) : nest{n} {}

    FerOp op;
    FerRepeatArray repeatArray; // always follows a FerOp with EndArray
    FerNesting nest; // always follows a FerOp with BeginVariant

    bool operator==(const FerCode &other) const noexcept
    {
        // ### The following doesn't check for possibly unused *bits*, but we aren't supposed to have any.
        // In any case, these checks are more like statements of intent :>
        static_assert(sizeof(FerOp) == sizeof(*this));
        static_assert(sizeof(FerRepeatArray) == sizeof(*this));
        static_assert(sizeof(FerNesting) == sizeof(*this));

        // Technically, this may give false positives if *this and other do not have the same active member,
        // but in typical use, operator==() is used on "real" FerCode vectors where types are determined by
        // preceding elements, which are either equal or fail the equality check before we even get to compare
        // subsequent differently typed elements.
        return std::memcmp(this, &other, sizeof(*this)) == 0;
    }
};


// boost::local_shared_ptr is not thread-safe, so no overhead due to atomics
#ifdef HAVE_BOOST
boost::local_shared_ptr<std::vector<FerCode>>
#else
std::shared_ptr<std::vector<FerCode>>
#endif
    DFERRY_EXPORT ferCodeForSignature(cstring signature,
                                      Arguments::SignatureType sigType = Arguments::MethodSignature,
                                      Arguments::FerEncodeOptions encodeOptions = Arguments::FerEncodeOptions::None);

std::string DFERRY_EXPORT printableFerOps(const std::vector<FerCode>& ops);

#endif // FERCODE_P_H
