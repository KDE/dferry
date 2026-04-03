#include "fercode_p.h"

#include "arguments_p.h"

#include <stack>
#include <string_view>
#include <unordered_map> // TODO faster container from boost?

#include <iostream> // TODO remove

struct BsHash
{
    std::size_t operator() (const std::pair<bool, std::string> &p) const noexcept
    {
        return std::hash<bool>{}(p.first) ^ std::hash<std::string>{}(p.second);
    }
};

thread_local static std::unordered_map<std::pair<bool, std::string>,
                                       std::shared_ptr<std::vector<FerCode>>,
                                       BsHash> encodedSignatureCache;

// Tracks current nesting levels as well as highest nesting levels seen so far.
// Why maxCombined? Imagine we're 60 variants deep and open a variant with 3 array depth and 3 paren depth.
// Does it exceed the max combined nesting depth of 64? If the 3 arrays don't contain any parens and
// vice versa, they don't. If it's 3 arrays inside 3 parens, they do. So we need to also know the max
// combined depth of parens + arrays.
struct NestingWithMax
{
    inline NestingWithMax() = default;

    inline bool beginArray()
    {
        array++;
        maxArray = std::max(maxArray, array);
        maxCombined = std::max(maxCombined, total());
        return likely(array <= Nesting::arrayMax && total() <= Nesting::totalMax);
    }

    inline void endArray()
    {
        assert(array >= 1);
        array--;
    }

    inline bool beginParen()
    {
        paren++;
        maxParen = std::max(maxParen, paren);
        maxCombined = std::max(maxCombined, total());
        return likely(paren <= Nesting::parenMax && total() <= Nesting::totalMax);
    }

    inline void endParen()
    {
        assert(paren >= 1);
        paren--;
    }

    inline bool beginVariant()
    {
        variant++;
        maxCombined = std::max(maxCombined, total());
        return likely(total() <= Nesting::totalMax);
    }

    inline void endVariant()
    {
        assert(variant >= 1);
        variant--;
    }

    inline uint32 total() const
    {
        return array + paren + variant;
    }

    uint32 array = 0;
    uint32 paren = 0;
    uint32 variant = 0;

    uint32 maxArray = 0;
    uint32 maxParen = 0;
    uint32 maxCombined = 0;
};

static void chopFirst(cstring *s)
{
    s->ptr++;
    s->length--;
}

static bool ferEncodeBasicType(cstring *s, NestingWithMax *nest, std::vector<FerCode>* out);

static bool ferEncodeSingleCompleteType(cstring *s, NestingWithMax *nest, std::vector<FerCode>* out)
{
    assert(s->ptr);
    // ### not checking if zero-terminated

    switch (*s->ptr) {
    case 'y':
    case 'b':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 'h': { // TODO that's a file descriptor, needs special treatment!!
        const TypeInfo& ty = typeInfo(*s->ptr);

        FerOpcode opcode = FerOpcode::Copy1;
        byte alignExponent = 0;
        const byte alignSize = typeInfo(*s->ptr).alignment;
        switch (alignSize) {
        case 2:
            opcode = FerOpcode::Copy2;
            alignExponent = 1;
            break;
        case 4:
            opcode = FerOpcode::Copy4;
            alignExponent = 2;
            break;
        case 8:
            opcode = FerOpcode::Copy8;
            alignExponent = 3;
            break;
        }

        out->push_back(FerOp{alignExponent, opcode, ty.state()});

        chopFirst(s);
        return true; }
    case 's':
        out->push_back(FerOp{2 /*align 4 for length field*/, FerOpcode::String, Arguments::String});

        chopFirst(s);
        return true;
    case 'o':
        out->push_back(FerOp{0, FerOpcode::ObjectPath, Arguments::ObjectPath});

        chopFirst(s);
        return true;
    case 'g':
        out->push_back(FerOp{0, FerOpcode::Signature, Arguments::Signature});

        chopFirst(s);
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }

        out->push_back(FerOp{0, FerOpcode::EnterVariant, Arguments::BeginVariant});
        out->push_back(FerNesting{static_cast<byte>(nest->array), static_cast<byte>(nest->paren)});

        chopFirst(s);
        nest->endVariant();
        return true;
    case '(': {
        if (!nest->beginParen()) {
            return false;
        }

        out->push_back(FerOp{3 /* 8 byte alignment */, FerOpcode::Copy0, Arguments::BeginStruct});

        chopFirst(s);
        bool isEmptyStruct = true;
        while (ferEncodeSingleCompleteType(s, nest, out)) {
            isEmptyStruct = false;
        }
        if (!s->length || *s->ptr != ')' || isEmptyStruct) {
            return false;
        }

        out->push_back(FerOp{0, FerOpcode::Copy0, Arguments::EndStruct});

        chopFirst(s);
        nest->endParen();
        return true; }
    case 'a': {
        if (!nest->beginArray()) {
            return false;
        }

        const size_t goBackIndex = out->size() + 1; // one past the BeginArray!

        chopFirst(s);
        if (*s->ptr == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || s->length < 4) {
                return false;
            }
            chopFirst(s);

            out->push_back(FerOp{2 /*align 4 for length field*/, FerOpcode::BeginArray, Arguments::BeginDict});

            // key must be a basic type
            if (!ferEncodeBasicType(s, nest, out)) {
                return false;
            }

            // Since key is the first element after the '('-like '{', it must be 8 byte aligned - patch it
            out->back().op.postAlignExponent = 3;

            // value can be any single complete type
            if (!ferEncodeSingleCompleteType(s, nest, out)) {
                return false;
            }

            if (!s->length || *s->ptr != '}') {
                return false;
            }
            chopFirst(s);
            nest->endParen();

            out->push_back(FerOp{0, FerOpcode::EndArray, Arguments::EndDict});
        } else { // regular array
            out->push_back(FerOp{2 /*align 4 for length field*/, FerOpcode::BeginArray, Arguments::BeginArray});

            if (!ferEncodeSingleCompleteType(s, nest, out)) {
                return false;
            }

            out->push_back(FerOp{0, FerOpcode::EndArray, Arguments::EndArray});
        }
        nest->endArray();

        // Instruction: rewind signature to this position for the next element of the array
        out->push_back(FerRepeatArray{0 /*to be patched in later*/, static_cast<uint16>(goBackIndex)});

        return true; }
    default:
        return false;
    }
}

static bool ferEncodeBasicType(cstring *s, NestingWithMax *nest, std::vector<FerCode>* out)
{
    switch (*s->ptr) {
    case 'y':
    case 'b':
    case 'n':
    case 'q':
    case 'i':
    case 'u':
    case 'x':
    case 't':
    case 'd':
    case 's':
    case 'o':
    case 'g':
    case 'h': // TODO is that really a basic type?
        return ferEncodeSingleCompleteType(s, nest, out);
    }
    return false;
}

static bool ferEncodeSignature(cstring *sig, Arguments::SignatureType type, std::vector<FerCode>* out)
{
    NestingWithMax nest;

    if (!sig->ptr || sig->length > 255 || sig->ptr[sig->length] != 0) {
        return false;
    }
    if (type == Arguments::VariantSignature) {
        out->push_back(FerOp{0, FerOpcode::BeginVariantSignature,
                       Arguments::NotStarted /*will be overwritten */});
        out->push_back(FerNesting{0, 0});
        out->push_back(FerNesting{0, 0});

        if (!sig->length) {
            return false;
        }
        if (!ferEncodeSingleCompleteType(sig, &nest, out)) {
            return false;
        }
        if (sig->length) {
            return false;
        }

        (*out)[1].nest.arrayDepth = nest.maxArray;
        (*out)[1].nest.parenDepth = nest.maxParen;
        (*out)[2].nest.arrayDepth /* really combinedDeptth */ = nest.maxCombined;

        out->push_back(FerOp{0, FerOpcode::EndVariant, Arguments::EndVariant});
    } else {
        out->push_back(FerOp{0, FerOpcode::BeginMethodSignature, Arguments::NotStarted /*ignored*/});

        while (sig->length) {
            if (!ferEncodeSingleCompleteType(sig, &nest, out)) {
                return false;
            }
        }

        out->push_back(FerOp{0, FerOpcode::End, Arguments::Finished});
    }

    assert(!nest.array);
    assert(!nest.paren);
    assert(!nest.variant);

    return true;
}

static std::string printableOpcode(FerOpcode op)
{
    switch (op) {
    case FerOpcode::Copy0:
        return "Copy0";
    case FerOpcode::Copy1:
        return "Copy1";
    case FerOpcode::Copy2:
        return "Copy2";
    case FerOpcode::Copy4:
        return "Copy4";
    case FerOpcode::Copy8:
        return "Copy8";
    case FerOpcode::String:
        return "String";
    case FerOpcode::ObjectPath:
        return "ObjectPath";
    case FerOpcode::Signature:
        return "Signature";
    case FerOpcode::BeginArray:
        return "BeginArray";
    case FerOpcode::EndArray:
        return "EndArray";
    case FerOpcode::EnterVariant:
        return "EnterVariant";
    case FerOpcode::BeginVariantSignature:
        return "BeginVariantSignature";
    case FerOpcode::EndVariant:
        return "EndVariant";
    case FerOpcode::BeginMethodSignature:
        return "BeginMethodSignature";
    case FerOpcode::End:
        return "End";
    }
    assert(false);
    return "?Opcode?";
}

static std::string printableAlignExponent(byte exponent)
{
    switch (exponent) {
    case 0:
        return "A0 ";
    case 1:
        return "A2 ";
    case 2:
        return "A4 ";
    case 3:
        return "A8 ";
    }

    assert(false);
    return " A? ";
}

static std::string printableFerOp(FerOp op)
{
    std::string align;

    return printableAlignExponent(op.postAlignExponent) + printableOpcode(op.op) + " " +
           printableState(op.ioState).ptr;
}

std::string printableFerOps(const std::vector<FerCode>& ops)
{
    std::string ret;
    for (size_t i = 0; i < ops.size(); i++) {
        const FerCode fc = ops[i];
        ret.append(printableFerOp(fc.op));

        if (fc.op.op == FerOpcode::BeginVariantSignature) {
            i += 2;
            if (i < ops.size()) {
                ret.append(" A");
                ret.append(std::to_string(ops[i - 1].nest.arrayDepth));
                ret.append(" P");
                ret.append(std::to_string(ops[i - 1].nest.parenDepth));
                ret.append(" C");
                ret.append(std::to_string(ops[i].nest.arrayDepth /* really combinedDepth */));
            } else {
                ret.append(" BeginVariantSignatureTrunc");
            }
        } else if (fc.op.op == FerOpcode::EndArray) {
            i++;
            if (i < ops.size()) {
                // These bytes are the "go back index" for the array, I suppose we could also print it?
                ret.append(" GoBack ");
                ret.append(std::to_string(ops[i].repeatArray.goBackOpIndex));
                ret.append(" post ");
                ret.append(printableAlignExponent(ops[i].repeatArray.goBackAlignExponent));
                ret.pop_back(); // remove trailing space
            } else {
                ret.append(" GoBackTrunc");
            }
        } else if (fc.op.op == FerOpcode::EnterVariant) {
            i++;
            if (i < ops.size()) {
                ret.append(" A");
                ret.append(std::to_string(ops[i].nest.arrayDepth));
                ret.append(" P");
                ret.append(std::to_string(ops[i].nest.parenDepth));
            } else {
                ret.append("EnterVariantTrunc");
            }
        }
        ret.append(", ");
    }
    if (ret.length() > 2) {
        ret.erase(ret.length() - 2); // remove trailing ", "
    }
    return ret;
}

static void optimizeFerOps(std::vector<FerCode> *ops);

std::shared_ptr<std::vector<FerCode>> ferCodeForSignature(cstring signature, Arguments::SignatureType sigType)
{
    // TODO
    // - Also cache "invalid signature" results?
    // - Heterogeneous lookup, using std::string_view as key. Main points:
    //   - seems relatively easy for ordered containers (but it's slow)
    //   - seems relatively difficult *and* requires C++20 for unordered containers (but it's fast)

    thread_local static std::string strSig;
    strSig.assign(signature.ptr, signature.length);
    const bool isVariant = sigType == Arguments::VariantSignature;

    auto it = encodedSignatureCache.find(std::make_pair(isVariant, strSig));
    if (it == encodedSignatureCache.cend()) {
        // cache pruning, TODO better algorithm
        if (encodedSignatureCache.size() > 128) {
            encodedSignatureCache.erase(encodedSignatureCache.begin());
        }

        std::vector<FerCode> ret;
        if (ferEncodeSignature(&signature, sigType, &ret)) {
            optimizeFerOps(&ret);
            it = encodedSignatureCache.emplace(std::make_pair(isVariant, strSig),
                                        std::make_shared<std::vector<FerCode>>(ret)).first;
        } else {
            // TODO? some way to provide information about the error
            return std::make_shared<std::vector<FerCode>>();
        }
    }

    return it->second;
}

static uint32 applyAlignment(uint32 addrSet, FerOp alignOp)
{
    assert(addrSet <= 0b11111111); // allowed values are just 1-8
    assert(addrSet != 0); // there must be some value (1 << 7 is the canonical representation of 8 ~= 0)

    switch (alignOp.postAlignExponent) {
    case 0:
        return addrSet;
    case 1:
        addrSet |= addrSet << 1;
        return addrSet & 0b10101010;
    case 2:
        addrSet |= addrSet << 1;
        addrSet |= addrSet << 2;
        return addrSet & 0b10001000;
    case 3:
        return 0b10000000;
    default:
        assert(false);
        return 0;
    }
}

static uint32 applyAddition(uint32 addrSet, FerOpcode addOp)
{
    assert(addrSet <= 0b11111111); // allowed addresses are just 1-8
    assert(addrSet != 0); // there must be some value (8 is equivalent to 0)

    uint32 addend;
    switch (addOp) {
    case FerOpcode::Copy1:
        addend = 1;
        break;
    case FerOpcode::Copy2:
        addend = 2;
        break;
    case FerOpcode::Copy4:
        addend = 4;
        break;
    case FerOpcode::Copy0:
    case FerOpcode::Copy8:
        // These don't change 8-alignment
        return addrSet;
    default:
        assert(false);
        return addrSet;
    }

    // rotate addrSet left ("add addend to all addresses")
    // here, we benefit from representing 0 as 8 because adding n (< 8) to 8 gives n, not 0!
    addrSet = addrSet << addend;
    addrSet |= addrSet >> 8;
    addrSet &= 0b11111111;

    return addrSet;
}

// ### we currently don't use "coarse grained" copy operations > Copy128 - these would require a little more
// work and don't seem to be very useful
static bool isBasicAdditionOp(FerOpcode op)
{
    return op >= FerOpcode::Copy1 && op <= FerOpcode::Copy8;
}

static bool isVarLengthOp(FerOpcode op)
{
    return op == FerOpcode::String || op == FerOpcode::ObjectPath ||
           op == FerOpcode::Signature || op == FerOpcode::EnterVariant;
}

struct ArrayAlignments
{
    uint32 contentsBeginAddrSet;    // addresses before first element of array (just after the length field);
                                    // if there are zero elements, also the end address of the array
    uint32 afterElementAddrSet;     // addresses after any element of array
    // Both of these addrSets are addresses *before* alignment to next array element!
};

// returns how far it has processed arrays (index into ops "pointing" to an EndArray)
static size_t optimizeArrays(std::vector<FerCode> *ops, uint32 addrSet, size_t beginArrayIndex,
                             std::unordered_map<size_t, ArrayAlignments> *arrayAlignments);

// after a variable length string, all alignments are possible - anyValues represent that (8 bits set)
static constexpr uint32 anyAddrSet = 0b11111111;

// TODO
// x left-shift ioState as well, with following knock-on effects...
// x EndArray stores the alignment for the element *after* the array
// x EndArray stores the ioState for the element *after* the array
// x the "GoBackIndex" element stores the alignment for *the first array element after looping back*
//   - and the ioState for looping back is taken from one element before GoBackIndex, i.e. the BeginArray
// x store variant nesting high water marks in two extra elements after BeginVariantSignature instead of
//   squashing a part into the first op - the first op will need to carry the ioState for the first element,
//   so there's no room to spare
static void optimizeFerOps(std::vector<FerCode> *ops)
{
    std::unordered_map<size_t, ArrayAlignments> arrayAlignments; // key: index of BeginArray
    std::stack<size_t> beginArrayIndexes;

    size_t prevAlignIndex = 0;
    size_t prevIoStateIndex = 0;
    const bool isVariant = (*ops)[0].op.op == BeginVariantSignature;

    // bitset of possible values, 1..8 because no alignment requirement greater 8 exists in DBus serialization,
    // so 8 ~= 0, 9 ~= 1 etc
    // we represent "full alignment" (8) as 8 instead of the equivalent 0 because that makes applyAddition
    // less weird (we'd have to special case 0 + n otherwise)
    uint32 addrSet = isVariant ? anyAddrSet : 0b10000000;

    for (size_t i = isVariant ? 3 : 1 ; i < ops->size(); i++) {

        assert(prevAlignIndex <= i);

        const uint32 prevAddrSet = addrSet;
        FerOp ferOp = (*ops)[i].op;


        // == Alignment

        // By the time we get to see the FerCode, the alignments haven't been left-shifted yet.
        // So alignment must be applied before the opcode with which it's stored.
        addrSet = applyAlignment(addrSet, ferOp);

        // "Shift left" (so that alignment and setting m_state for the *next* op can be processed as part of
        // handling the *current* op), also eliminate alignment operations that do nothing
        if (addrSet != prevAddrSet) {
            (*ops)[prevAlignIndex].op.postAlignExponent = ferOp.postAlignExponent;
        }
        (*ops)[prevIoStateIndex].op.ioState = ferOp.ioState;

        (*ops)[i].op.postAlignExponent = 0;
        (*ops)[i].op.ioState = Arguments::InvalidData; // ### or something more inert maybe? Let's see in the tests


        // == Payload data

        if (isBasicAdditionOp(ferOp.op)) {
            addrSet = applyAddition(addrSet, ferOp.op);
        }

        if (ferOp.op != FerOpcode::Copy0) {
            // We can collapse consecutive alignments into one, but that cannot cross operations that
            // actually read or write data. So if the current operation does something with data,
            // after it (note that we're left shifting) becomes the new target for alignment merging.
            prevAlignIndex = i;
        }
        prevIoStateIndex = i;


        if (ferOp.op == FerOpcode::BeginArray) {
            // Arrays are the most difficult part of this!
            // - Array elements can repeat, and subsequent elements can start and end at differently
            //   aligned addresses from the first element
            // - There can be arrays inside arrays, oh my...
            // To deal with this, we use optimizeArray to gather data (including for nested arrays inside
            // the outermost one that we find) and then do a final pass where we apply the usual optimization.

            addrSet = applyAddition(addrSet, FerOpcode::Copy4); // array length field!

            auto arrAlignIt = arrayAlignments.find(i);
            if (arrAlignIt == arrayAlignments.cend()) {
                // Seeing this BeginArray for the first time - pre-process it. Note that we use
                // optimizeArrays only for data gathering, we don't skip ahead our own parsing at all.
                optimizeArrays(ops, addrSet, i, &arrayAlignments);

                arrAlignIt = arrayAlignments.find(i);
                assert(arrAlignIt != arrayAlignments.cend());
            }

            const ArrayAlignments arrayAlign = arrAlignIt->second;
            // We now do just *one* pass through the array in which we must consider all possible alignments
            // (as determined by optimizeArray) at the same time. If we are before the beginning of the nth
            // element, the address is the end of the (n - 1)th element, so afterElementAddrSet is included.
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

            beginArrayIndexes.push(i);

        } else if (ferOp.op == FerOpcode::EndArray) {

            const size_t beginArrayIndex = beginArrayIndexes.top();
            beginArrayIndexes.pop();
            assert((*ops)[beginArrayIndex].op.op == FerOpcode::BeginArray);

            auto arrAlignIt = arrayAlignments.find(beginArrayIndex);
            assert(arrAlignIt != arrayAlignments.cend());
            ArrayAlignments &arrayAlign = arrAlignIt->second;

            size_t goBackIndex = beginArrayIndex + 1;

            // If alignment is needed after the array length field, but not for any other elements
            // (i.e. after the first), *skip* the alignment when going back!
            // No alignment req'd only for first element can happen, but is probably not worth handling.
            const FerCode beginArrayOp = (*ops)[beginArrayIndex];
            byte loopBackAlignExponent = beginArrayOp.op.postAlignExponent;
            if (loopBackAlignExponent) {
                const uint32 afterElementAlignedAddrs = applyAlignment(arrayAlign.afterElementAddrSet,
                                                                       beginArrayOp.op);
                if (afterElementAlignedAddrs == arrayAlign.afterElementAddrSet) {
                    // alignment did nothing -> is not necessary -> remove it
                    loopBackAlignExponent = 0;
                }
            }

            // contentsBeginAddrSet is included because the array may contain zero elements
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

            (*ops)[++i] = FerRepeatArray{loopBackAlignExponent, static_cast<uint16>(goBackIndex)};

            if (beginArrayIndexes.empty()) {
                // leaving the outermost level of nested array, we won't need that anymore
                arrayAlignments.clear();
            }

        } else {
            // After strings, variants and such, data alignment could be anything
            if (isVarLengthOp(ferOp.op)) {
                addrSet = anyAddrSet;
            }
        }
    }

    assert(arrayAlignments.empty());
    assert(beginArrayIndexes.empty());
}

static size_t optimizeArrays(std::vector<FerCode> *ops, uint32 addrSet, size_t beginArrayIndex,
                             std::unordered_map<size_t, ArrayAlignments> *arrayAlignments)
{
    assert((*ops)[beginArrayIndex].op.op == FerOpcode::BeginArray);

    {
        auto arrAlignIt = arrayAlignments->find(beginArrayIndex);
        if (arrAlignIt == arrayAlignments->cend()) {
            arrAlignIt = arrayAlignments->emplace_hint(arrAlignIt /*hint*/,
                                                std::make_pair(beginArrayIndex, ArrayAlignments{0, 0}));
        }
        arrAlignIt->second.contentsBeginAddrSet |= addrSet;
    }

    for (size_t i = beginArrayIndex + 1; ; i++) {
        const FerOp ferOp = (*ops)[i].op;

        addrSet = applyAlignment(addrSet, ferOp);

        if (isBasicAdditionOp(ferOp.op)) {
            addrSet = applyAddition(addrSet, ferOp.op);
        } else if (isVarLengthOp(ferOp.op)) {
            addrSet = anyAddrSet;
        } else if (ferOp.op == FerOpcode::BeginArray) {
            // Since we skip "our" BeginArray, what we have here is another array inside the current one

            addrSet = applyAddition(addrSet, FerOpcode::Copy4); // array length field!
            const size_t beginArrayIndex = i;

            i = optimizeArrays(ops, addrSet, i, arrayAlignments);
            assert((*ops)[i].op.op == FerOpcode::EndArray);
            i++; // skip FerEndArray

            // Our data position is now after the last element of the inner array.
            // The array may be empty, so our position could be right after the array length field as well.
            auto innerAlignIt = arrayAlignments->find(beginArrayIndex);
            assert(innerAlignIt != arrayAlignments->cend());
            ArrayAlignments &arrayAlign = innerAlignIt->second;
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

        } else if (ferOp.op == FerOpcode::EndArray) {
            bool noMoreAddrSetChanges = false;

            auto innerAlignIt = arrayAlignments->find(beginArrayIndex);
            assert(innerAlignIt != arrayAlignments->cend());
            ArrayAlignments &arrayAlign = innerAlignIt->second;
            if (arrayAlign.afterElementAddrSet) {
                // this is not our first pass through that array
                addrSet |= arrayAlign.afterElementAddrSet;
                // if addrSet had no bits that are not in afterElementAddrSet, we have not seen any new
                // addresses, and doing more iterations will only repeat previous results -> we're done
                noMoreAddrSetChanges = arrayAlign.afterElementAddrSet == addrSet;
            } else {
                // This *is* our first pass through that array. If the alignment before and after the first
                // element is the same, we're already done.
                noMoreAddrSetChanges = arrayAlign.contentsBeginAddrSet == addrSet;
            }

            arrayAlign.afterElementAddrSet = addrSet;

            if (noMoreAddrSetChanges) {
                return i;
            } else {
                // do more passes through the array until we have seen all addresses
                i = beginArrayIndex /* note, the for loop does in++ */;
            }
        }
    }
    return 0;
    assert(false);
}
