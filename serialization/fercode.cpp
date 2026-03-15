#include "fercode_p.h"

#include "arguments_p.h"

#include <stack>
#include <unordered_map> // TODO faster container from boost?

static void chopFirst(cstring *s)
{
    s->ptr++;
    s->length--;
}

static bool ferEncodeBasicType(cstring *s, Nesting *nest, std::vector<FerOp>* out);

static bool ferEncodeSingleCompleteType(cstring *s, Nesting *nest, std::vector<FerOp>* out)
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
        const byte alignSize = typeInfo(*s->ptr).alignment;
        switch (alignSize) {
        case 2:
            out->push_back(FerOp::Align2);
            break;
        case 4:
            out->push_back(FerOp::Align4);
            break;
        case 8:
            out->push_back(FerOp::Align8);
            break;
        }
        out->push_back(static_cast<FerOp>(alignSize));

        chopFirst(s);
        return true; }
    case 's':
        out->push_back(FerOp::String);

        chopFirst(s);
        return true;
    case 'o':
        out->push_back(FerOp::ObjectPath);

        chopFirst(s);
        return true;
    case 'g':
        out->push_back(FerOp::Signature);

        chopFirst(s);
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }

        out->push_back(FerOp::BeginVariant);
        out->push_back(static_cast<FerOp>(nest->array));
        out->push_back(static_cast<FerOp>(nest->paren));
        out->push_back(static_cast<FerOp>(nest->variant));

        // TODO? some kind of transition to "dynamic" variant signature and payload parsing...
        // but maybe this info is enough for the "dynamic" code to take over.
        chopFirst(s);
        nest->endVariant();
        return true;
    case '(': {
        if (!nest->beginParen()) {
            return false;
        }

        out->push_back(FerOp::Align8);

        chopFirst(s);
        bool isEmptyStruct = true;
        while (ferEncodeSingleCompleteType(s, nest, out)) {
            isEmptyStruct = false;
        }
        if (!s->length || *s->ptr != ')' || isEmptyStruct) {
            return false;
        }
        chopFirst(s);
        nest->endParen();
        return true; }
    case 'a': {
        if (!nest->beginArray()) {
            return false;
        }

        // The alignmnt is explicit so it can be subject to regular alignment elision optimization.
        out->push_back(FerOp::Align4);

        // The reading of the array length field, though, is implicit!
        out->push_back(FerOp::BeginArray);
        const size_t goBackAddress = out->size(); // one past the BeginArray!

        chopFirst(s);
        if (*s->ptr == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || s->length < 4) {
                return false;
            }
            chopFirst(s);

            out->push_back(FerOp::Align8); // "dict entries" are similar to structs, and like them, 8-aligned

            // key must be a basic type
            if (!ferEncodeBasicType(s, nest, out)) {
                return false;
            }
            // value can be any single complete type
            if (!ferEncodeSingleCompleteType(s, nest, out)) {
                return false;
            }
            if (!s->length || *s->ptr != '}') {
                return false;
            }
            chopFirst(s);
            nest->endParen();
        } else { // regular array
            if (!ferEncodeSingleCompleteType(s, nest, out)) {
                return false;
            }
        }
        nest->endArray();

        out->push_back(FerOp::EndArray);
        // push two bytes forming an LSB first 16 bit integer telling where to go back for the BeginArray
        out->push_back(static_cast<FerOp>(goBackAddress & 0xff));
        out->push_back(static_cast<FerOp>((goBackAddress & 0xff00) >> 8));

        return true; }
    default:
        return false;
    }
}

static bool ferEncodeBasicType(cstring *s, Nesting *nest, std::vector<FerOp>* out)
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

static bool ferEncodeSignature(cstring *sig, Arguments::SignatureType type, std::vector<FerOp>* out)
{
    Nesting nest;
    if (!sig->ptr || sig->length > 255 || sig->ptr[sig->length] != 0) {
        return false;
    }
    if (type == Arguments::VariantSignature) {
        if (!sig->length) {
            return false;
        }
        if (!ferEncodeSingleCompleteType(sig, &nest, out)) {
            return false;
        }
        if (sig->length) {
            return false;
        }
    } else {
        while (sig->length) {
            if (!ferEncodeSingleCompleteType(sig, &nest, out)) {
                return false;
            }
        }
    }

    assert(!nest.array);
    assert(!nest.paren);
    assert(!nest.variant);
    out->push_back(FerOp::End);
    return true;
}

std::string printableFerOp(FerOp op)
{
    if (op == FerOp::End) {
        return "End";
    } else if (op >= FerOp::Copy1 && op <= FerOp::Copy4096) {
        uint num = 0;
        if (op < FerOp::Copy256) {
            num = static_cast<byte>(op);
        } else {
            num = 256 << (static_cast<byte>(op) - static_cast<byte>(FerOp::Copy256));
        }
        return "Copy" + std::to_string(num);
    } else if (op == FerOp::Align2) {
        return "Align2";
    } else if (op == FerOp::Align4) {
        return "Align4";
    } else if (op == FerOp::Align8) {
        return "Align8";
    } else if (op == FerOp::String) {
        return "String";
    } else if (op == FerOp::ObjectPath) {
        return "ObjectPath";
    } else if (op == FerOp::Signature) {
        return "Signature";
    } else if (op == FerOp::BeginArray) {
        return "BeginArray";
    } else if (op == FerOp::EndArray) {
        return "EndArray";
    } else if (op == FerOp::BeginVariant) {
        return "BeginVariant";
    } else if (op == FerOp::Error) {
        return "Error";
    } else {
        return "?" + std::to_string(static_cast<uint>(op));
    }
}

std::string printableFerOps(const std::vector<FerOp>& ops)
{
    std::string ret;
    for (size_t i = 0; i < ops.size(); i++) {
        const FerOp op = ops[i];
        ret.append(printableFerOp(op));
        if (op == FerOp::EndArray) {
            if (i + 2 < ops.size()) {
                // These bytes are the "go back index" for the array, I suppose we could also print it?
                const uint32 goBackIndex = static_cast<byte>(ops[i + 1]) +
                                           (static_cast<byte>(ops[i + 2]) << 8);
                ret.append("GoBack");
                ret.append(std::to_string(goBackIndex));
            } else {
                ret.append("GoBackTrunc");
            }

            i += 2;
        }
        ret.append(", ");
    }
    ret.pop_back();
    ret.pop_back();
    return ret;
}

static void optimizeFerOps(std::vector<FerOp> *ops, bool mergeContiguousCopies);

//static
std::vector<FerOp> ferOpsForSignature(cstring signature, bool optimize, bool mergeContiguousCopies)
{
    std::vector<FerOp> ret;
    if (ferEncodeSignature(&signature, Arguments::MethodSignature, &ret)) {
        if (optimize) {
            optimizeFerOps(&ret, mergeContiguousCopies);
        }
    } else {
        ret.clear();
        ret.push_back(FerOp::Error);
    }
    return ret;
}

static uint32 applyAlignment(uint32 addrSet, FerOp alignOp)
{
    assert(addrSet <= 0b11111111); // allowed values are just 1-8
    assert(addrSet != 0); // there must be some value (1 << 7 is the canonical representation of 8 ~= 0)

    switch (alignOp) {
    case FerOp::Align2:
        addrSet |= addrSet << 1;
        return addrSet & 0b10101010;
    case FerOp::Align4:
        addrSet |= addrSet << 1;
        addrSet |= addrSet << 2;
        return addrSet & 0b10001000;
    case FerOp::Align8:
        return 0b10000000;
    default:
        assert(false);
        return 0;
    }
}

static uint32 applyAddition(uint32 addrSet, FerOp addOp)
{
    assert(addrSet <= 0b11111111); // allowed addresses are just 1-8
    assert(addrSet != 0); // there must be some value (8 is equivalent to 0)

    if (addOp < FerOp::Copy1 || addOp > FerOp::Copy127) {
        // others are either not additions or preserve 8-alignment
        return addrSet;
    }
    uint32 addend = uint32(addOp); // enum values are chosen for this to work
    // only keep the part that isn't 8-aligned
    addend &= 0x7;

    // rotate addrSet left ("add addend to all addresses")
    // here, we benefit from representing 0 as 8 because adding n (< 8) to 8 gives n, not 0!
    addrSet = addrSet << addend;
    addrSet |= addrSet >> 8;
    addrSet &= 0b11111111;

    return addrSet;
}

static bool isAlignOp(FerOp op)
{
    return op >= FerOp::Align2 && op <= FerOp::Align8;
}

// ### we currently don't use "coarse grained" copy operations > Copy128 - these would require a little more
// work and don't seem to be very useful
static bool isBasicAdditionOp(FerOp op)
{
    return op >= FerOp::Copy1 && op <= FerOp::Copy128;
}

static bool isVarLengthOp(FerOp op)
{
    return op == FerOp::String || op == FerOp::ObjectPath || op == FerOp::Signature ||
           op == FerOp::BeginVariant;
}

struct ArrayAlignments
{
    uint32 contentsBeginAddrSet;    // addresses before first element of array (just after the length field);
                                    // if there are zero elements, also the end address of the array
    uint32 afterElementAddrSet;     // addresses after any element of array
    // Both of these addrSets are addresses *before* alignment to next array element!
};

// returns how far it has processed arrays (index into ops "pointing" to an EndArray)
static size_t optimizeArrays(std::vector<FerOp> *ops, uint32 addrSet, size_t beginArrayIndex,
                             std::unordered_map<size_t, ArrayAlignments> *arrayAlignments);

// after a variable length string, all alignments are possible - anyValues represent that (8 bits set)
static constexpr uint32 anyAddrSet = 0b11111111;

// TODO also allow an arbitrary starting alignment to make it work for variant signatures. Or change
// the format to always begin with an alignment, for reusability of all (single complete type) signatures as
// variant signatures.
static void optimizeFerOps(std::vector<FerOp> *ops, bool mergeContiguousCopies)
{
    // bitset of possible values, 1..8 because no alignment requirement greater 8 exists in DBus serialization,
    // so 8 ~= 0, 9 ~= 1 etc
    // we represent "full alignment" (8) as 8 instead of the equivalent 0 because that makes applyAddition
    // less weird (we'd have to special case 0 + n otherwise)
    uint32 addrSet = 0b10000000;

    std::unordered_map<size_t, ArrayAlignments> arrayAlignments; // key: index of BeginArray
    std::stack<size_t> beginArrayIndexes;

    size_t out = 0;
    FerOp prevOp = FerOp::End;
    for (size_t in = 0; in < ops->size(); in++) {

        assert(out <= in); // we sometimes skip / eliminate / merge / rewrite ops, we never add new ones

        const uint32 prevAddrSet = addrSet;
        FerOp ferOp = (*ops)[in];

        assert(!(ferOp >= FerOp::Copy256 && ferOp <= FerOp::Copy4096)); // not currently supported

        if (isBasicAdditionOp(ferOp)) {

            addrSet = applyAddition(addrSet, ferOp);

            if (mergeContiguousCopies && isBasicAdditionOp(prevOp)) {
                const FerOp mergedOp = static_cast<FerOp>(uint32(prevOp) + uint32(ferOp));
                if (isBasicAdditionOp(mergedOp)) { // still in basic addition range?
                    ferOp = mergedOp;
                    out--; // overwrite previous op!
                }
            }
            (*ops)[out++] = ferOp;

        } else if (isAlignOp(ferOp)) {

            addrSet = applyAlignment(addrSet, ferOp);

            if (isAlignOp(prevOp)) {
                // Merge consecutive alignment operations (preferring the larger one even if it changes nothing)
                ferOp = std::max(ferOp, prevOp);
                // overwrite the previous operation with the new merged one
                out--;
                (*ops)[out++] = ferOp;
            } else {
                // Eliminate alignment operations that do nothing
                if (addrSet != prevAddrSet) {
                    (*ops)[out++] = ferOp;
                } else {
                    // remove this operation from the output entirely (no out++)
                    ferOp = prevOp;
                }
            }

        } else if (ferOp == FerOp::BeginArray) {
            // Arrays are the most difficult part of this!
            // - Array elements can repeat, and subsequent elements can start and end at differently
            //   aligned addresses from the first element
            // - There can be arrays inside arrays, oh my...
            // To deal with this, we use optimizeArray to gather data (including for nested arrays inside
            // the outermost one that we find) and then do a final pass where we apply the usual optimization.

            addrSet = applyAddition(addrSet, FerOp::Copy4); // array length field!

            auto arrAlignIt = arrayAlignments.find(in);
            if (arrAlignIt == arrayAlignments.cend()) {
                // Seeing this BeginArray for the first time - pre-process it. Note that we use
                // optimizeArrays only for data gathering, we don't skip ahead our own parsing at all.
                optimizeArrays(ops, addrSet, in, &arrayAlignments);

                arrAlignIt = arrayAlignments.find(in);
                assert(arrAlignIt != arrayAlignments.cend());
            }

            const ArrayAlignments arrayAlign = arrAlignIt->second;
            // We now do just *one* pass through the array in which we must consider all possible alignments
            // (as determined by optimizeArray) at the same time. If we are before the beginning of the nth
            // element, the address is the end of the (n - 1)th element, so afterElementAddrSet is included.
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

            // re-insert the ArrayAlignment entry under its new index in ops!
            arrayAlignments.erase(arrAlignIt);
            arrayAlignments.emplace(std::make_pair(out, arrayAlign));

            beginArrayIndexes.push(out);
            (*ops)[out++] = ferOp;

        } else if (ferOp == FerOp::EndArray) {

            const size_t beginArrayIndex = beginArrayIndexes.top();
            beginArrayIndexes.pop();
            assert((*ops)[beginArrayIndex] == FerOp::BeginArray);

            auto arrAlignIt = arrayAlignments.find(beginArrayIndex);
            assert(arrAlignIt != arrayAlignments.cend());
            ArrayAlignments &arrayAlign = arrAlignIt->second;

            size_t goBackIndex = beginArrayIndex + 1;

            // If alignment is needed after the array length field, but not for any other elements
            // (i.e. after the first), *skip* the alignment instruction when going back!
            // No alignment req'd only for first element can happen, but is probably not worth handling.
            const FerOp maybeAlignOp = (*ops)[goBackIndex];
            if (isAlignOp(maybeAlignOp)) {
                const uint32 afterElementAlignedAddrs = applyAlignment(arrayAlign.afterElementAddrSet,
                                                                       maybeAlignOp);
                if (afterElementAlignedAddrs == arrayAlign.afterElementAddrSet) {
                    // alignment did nothing -> is not necessary -> skip it!
                    goBackIndex += 1;
                }
            }

            // contentsBeginAddrSet is included because the array may contain zero elements
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

            (*ops)[out++] = ferOp;
            (*ops)[out++] = static_cast<FerOp>(goBackIndex & 0xff);
            (*ops)[out++] = static_cast<FerOp>((goBackIndex & 0xff00) >> 8);
            in += 2; // skip "go back index"

            if (beginArrayIndexes.empty()) {
                // leaving the outermost level of nested array, we won't need that anymore
                arrayAlignments.clear();
            }

        } else {
            if (isVarLengthOp(ferOp)) {
                addrSet = anyAddrSet;
            }
            (*ops)[out++] = ferOp;
        }

        // ### do not use "continue" in the loop because of this!
        prevOp = ferOp;
    }

    assert(arrayAlignments.empty());
    assert(beginArrayIndexes.empty());

    ops->resize(out);
}

static size_t optimizeArrays(std::vector<FerOp> *ops, uint32 addrSet, size_t beginArrayIndex,
                             std::unordered_map<size_t, ArrayAlignments> *arrayAlignments)
{
    assert((*ops)[beginArrayIndex] == FerOp::BeginArray);

    {
        auto arrAlignIt = arrayAlignments->find(beginArrayIndex);
        if (arrAlignIt == arrayAlignments->cend()) {
            arrAlignIt = arrayAlignments->emplace_hint(arrAlignIt /*hint*/,
                                                std::make_pair(beginArrayIndex, ArrayAlignments{0, 0}));
        }
        arrAlignIt->second.contentsBeginAddrSet |= addrSet;
    }

    for (size_t in = beginArrayIndex + 1; ; in++) {
        const FerOp ferOp = (*ops)[in];

        if (isBasicAdditionOp(ferOp)) {
            addrSet = applyAddition(addrSet, ferOp);
        } else if (isAlignOp(ferOp)) {
            addrSet = applyAlignment(addrSet, ferOp);
        } else if (ferOp == FerOp::BeginArray) {
            // Since we skip "our" BeginArray, what we have here is another array inside the current one

            addrSet = applyAddition(addrSet, FerOp::Copy4); // array length field!
            const size_t beginArrayIndex = in;

            in = optimizeArrays(ops, addrSet, in, arrayAlignments);
            assert((*ops)[in] == FerOp::EndArray);
            in += 2; // skip "go back index"

            // Our position is now after the last element of the inner array.
            // The array may be empty, so our position could be right after the array length field as well.
            auto innerAlignIt = arrayAlignments->find(beginArrayIndex);
            assert(innerAlignIt != arrayAlignments->cend());
            ArrayAlignments &arrayAlign = innerAlignIt->second;
            addrSet = arrayAlign.contentsBeginAddrSet | arrayAlign.afterElementAddrSet;

        } else if (ferOp == FerOp::EndArray) {
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
                return in;
            } else {
                // do more passes through the array until we have seen all addresses
                in = beginArrayIndex /* note, the for loop does in++ */;
            }
        } else {
            if (isVarLengthOp(ferOp)) {
                addrSet = anyAddrSet;
            }
        }
    }
    return 0;
    assert(false);
}
