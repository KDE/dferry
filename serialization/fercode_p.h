#ifndef FERCODE_P_H
#define FERCODE_P_H

#include "arguments.h"

enum FerOpcode : byte
{
    Copy0 = 0,// No-op (for e.g. IoState BeginStruct and EndStruct, which only align and change state)
    Copy1,
    Copy2,
    Copy4,
    Copy8,
    String,
    ObjectPath,
    Signature,
    BeginArray,     // TODO? BeginFixedArray where element count can be trivially calculated from length
    EndArray,       // alignment in this case is for the "go back" case, alignment for the "end of array"
                    // case is in EndArrayData.
                    // Always followed by a FerEndArray.
    EnterVariant,   // Always followed by a FerNesting giving the nesting depth where the variant
                    // occurs, so current nesting + variant's nesting can be checked against limits

    BeginVariantSignature,  // Single complete type / Always followed by FerNesting /
                            // assumes data starting from an unaligned address
    EndVariant,

    BeginMethodSignature,   // Never followed by FerNesting / assumes starting from an 8-byte aligned address
    End,
};

struct FerOp
{
    byte postAlignExponent : 2; // this is for the *following* element!
    FerOpcode op : 6;
    Arguments::IoState ioState;
};

struct FerEndArray
{
    byte afterArrayAlignExponent : 2;         // for the "end of array" case
    uint16 repeatArrayIndex : 14;
};

struct FerNesting
{
    byte arrayDepth;
    byte parenDepth;
};

// We need another couple of bits for FerNesting after BeginVariantSignature to store the max combined
// aggregate depth. For that, we type-pun the BeginVariantSignature FerOp with this to store
// combinedDepth in the ioState field.
struct FerBeginVariantSpecial
{
    byte postAlignExponent : 2;
    FerOpcode op : 6;
    byte combinedDepth;
};

union FerCode
{
    FerCode(FerOp o) : op{o} {}
    FerCode(FerEndArray a) : endArray{a} {}
    FerCode(FerNesting n) : nest{n} {}
    FerCode(FerBeginVariantSpecial bv) : beginVariantSpecial{bv} {}

    FerOp op;
    FerEndArray endArray; // always follows a FerOp with BeginArray
    FerNesting nest; // always follows a FerOp with BeginVariant
    FerBeginVariantSpecial beginVariantSpecial; // always type-punned with op for a BeginVariantSignature

    bool operator==(const FerCode &other) const
    {
        // Just compare all the bits, nest is maybe the cleanest / easiest way to do that
        return nest.arrayDepth == other.nest.arrayDepth &&
               nest.parenDepth == other.nest.parenDepth;
    }
};

std::vector<FerCode> DFERRY_EXPORT ferCodeForSignature(cstring signature,
                                            Arguments::SignatureType sigType = Arguments::MethodSignature);
std::string DFERRY_EXPORT printableFerOps(const std::vector<FerCode>& ops);

#endif // FERCODE_P_H
