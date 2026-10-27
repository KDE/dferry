// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "argumentsbcreader.h"

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
    m_state = Arguments::Finished; d->mergeError(errCode); return s_nullBuffer; }


static inline const byte *align(const byte *index, uintptr_t alignment)
{
    // it also works with 1, but makes no sense
    if (alignment < 2 || alignment > 8) {
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
    const FerCode *m_opsBase{};
    Nesting m_nesting;
    Error m_error;
    bool m_validateUtf8 = true;

#ifdef HAVE_BOOST
    boost::local_shared_ptr<std::vector<FerCode>> m_ops;
    boost::container::small_vector<const byte *, 8> m_arrayLengthStack; // stores the limits of arrays
#else
    std::shared_ptr<std::vector<FerCode>> m_ops;
    std::vector<const byte *> m_arrayLengthStack;
#endif
    std::vector<VariantInfo> m_variantStack;
    std::vector<int> m_fileDescriptors;

    void setDataFrom(const Arguments& args)
    {
        const chunk& data = Arguments::Private::of(&args)->m_data;
        m_dataPtr = data.ptr;
        m_dataEnd = data.ptr + data.length;
        m_fileDescriptors = args.fileDescriptors();
    }

    void mergeError(Error::Code errorCode);
};

void ArgumentsBcReader::Private::mergeError(Error::Code errorCode)
{
    if (!m_error.isError()) {
        m_error.setCode(errorCode);
    }
}

static const char s_nullBuffer[16] {}; // inert fake data for callers when reading bad or nonexistent data

/** \class ArgumentsBcReader
    Reads arguments out of Arguments using type signature byte-code.

    This class is less flexible, but generally about 2x as fast as ArgumentsReader if the bytecode
    already exists.

    \see Arguments
*/

/// Constructs an %ArgumentsBcReader to read arguments from \p args. A preprocessed type signature according
/// to \p encodeOptions is taken from a thread-local cache or created if necessery.
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, Arguments::FerEncodeOptions encodeOptions)
    : d(new Private)
{
    d->m_ops = ferCodeForSignature(args.signature(), Arguments::MethodSignature, encodeOptions);
    d->setDataFrom(args);
    beginRead();
}

/// Constructs an %ArgumentsBcReader to read arguments from \p msg. A preprocessed type signature according
/// to \p encodeOptions is taken from a thread-local cache or created if necessery.
/// This is a convenience constructor that just avoids calling msg->arguments().
ArgumentsBcReader::ArgumentsBcReader(const Message &msg, Arguments::FerEncodeOptions encodeOptions)
    : d(new Private)
{
    const Arguments &args = msg.arguments();
    d->m_ops = ferCodeForSignature(args.signature(), Arguments::MethodSignature, encodeOptions);
    d->setDataFrom(args);
    beginRead();
}

/// Constructs an %ArgumentsBcReader to read arguments from \p args, using a prep-processed type signature
/// in \p ferCode.
#ifdef HAVE_BOOST
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, boost::local_shared_ptr<std::vector<FerCode>> ferCode)
#else
ArgumentsBcReader::ArgumentsBcReader(const Arguments &args, std::shared_ptr<std::vector<FerCode>> ferCode)
#endif
    : d(new Private)
{
    d->m_ops = ferCode;
    d->setDataFrom(args);
    beginRead();
}

/// Constructs an %ArgumentsReader to read arguments from \p msg, using a prep-processed type signature
/// in \p ferCode.
/// This is a convenience constructor that just avoids calling msg->arguments().
#ifdef HAVE_BOOST
ArgumentsBcReader::ArgumentsBcReader(const Message &msg, boost::local_shared_ptr<std::vector<FerCode>> ferCode)
#else
ArgumentsBcReader::ArgumentsBcReader(const Message &msg, std::shared_ptr<std::vector<FerCode>> ferCode)
#endif
{
    const Arguments &args = msg.arguments();
    d->m_ops = ferCode;
    d->setDataFrom(args);
    beginRead();
}

/// Takes ownership of \p other's data and makes \p other invalid.
ArgumentsBcReader::ArgumentsBcReader(ArgumentsBcReader &&other)
    : m_state(other.m_state),
      d(other.d)
{
    other.d = nullptr;
}

/// Copies \p other with all of its state, including read position.
ArgumentsBcReader::ArgumentsBcReader(const ArgumentsBcReader &other)
    : m_state(other.m_state),
      d(nullptr)

{
    if (other.d) {
        d = new Private(*other.d);
    }
}

/// Takes ownership of \p other's data and makes \p other invalid.
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

/// Copies \p other with all of its state, including read position.
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

/// \fn Arguments::IoState ArgumentsBcReader::state() const
/// \returns the type of the next element. In case of error or end of data, the return
///          value will indicate that instead.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \returns the current state() as a string
cstring ArgumentsBcReader::stateString() const
{
    return printableState(m_state);
}

/// \returns the error that occured on this reader, if any. Otherwise, the return value has error
///          code Error::NoError.
Error ArgumentsBcReader::error() const
{
    return d->m_error;
}

/// \returns wnether the reader is in an error state. Error states are not recoverable.
bool ArgumentsBcReader::isError() const
{
    return d->m_error.isError();
}

/// \returns wnether the reader is in a final state, may be success or error.
/// \see isError()
bool ArgumentsBcReader::isFinished() const
{
    return m_state == Arguments::Finished;
}

/// \returns isFinished() && !isError()
bool ArgumentsBcReader::isSuccess() const
{
    return m_state == Arguments::Finished && !d->m_error.isError();
}

/// Sets whether the reader fully validates the UTF-8 encoding of strings.
/// \see validatesUtf8
void ArgumentsBcReader::setValidatesUtf8(bool validate)
{
    d->m_validateUtf8 = validate;
}

/// \returns whether the reader fully validates the UTF-8 encoding of strings.
/// The reader always validates that strings do note exceeed maximum allowed Length and have a null
/// terminator. This property controls whether string are also checked to be valid UTF-8 and not
/// contain null bytes before the end. The default value is \c true.
/// Disabling UTF-8 validation is faster, but insecure if the sender is not trusted.
/// \see setValidatesUtf8
bool ArgumentsBcReader::validatesUtf8() const
{
    return d->m_validateUtf8;
}

bool ArgumentsBcReader::beginArrayInternal()
{
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
        if (m_state == Arguments::BeginArray) {
            m_state = Arguments::EndArray;
        } else {
            m_state = Arguments::EndDict;
        }

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

/// Starts reading an array.
/// Required state: Arguments::BeginArray.
/// \returns true if the array has >= 1 elements, false if it has 0.
bool ArgumentsBcReader::beginArray()
{
    if (unlikely(m_state != Arguments::BeginArray)) {
        m_state = Arguments::Finished;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }
    return beginArrayInternal();
}

/// Ends reading an array.
/// Required state: Arguments::EndArray.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endArray()
{
    advanceStateAggregate(Arguments::EndArray);
}

/// Starts reading a dict.
/// Required state: Arguments::BeginDict.
/// \returns true if the dict has >= 1 elements, false if it has 0.
bool ArgumentsBcReader::beginDict()
{
    if (unlikely(m_state != Arguments::BeginDict)) {
        m_state = Arguments::Finished;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }
    return beginArrayInternal();
}

/// Ends reading a dict.
/// Required state: Arguments::EndDict.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endDict()
{
    advanceStateAggregate(Arguments::EndDict);
}

/// \fn void ArgumentsBcReader::beginStruct()
/// Begins reading a struct.
/// Required state: Arguments::BeginStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcReader::endStruct()
/// Ends reading a struct.
/// Required state: Arguments::EndStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// Begins reading a variant.
/// Required state:  Arguments::BeginVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::beginVariant()
{
    advanceStateAggregate(Arguments::BeginVariant);
}

/// Ends reading a variant.
/// Required state: Arguments::EndVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcReader::endVariant()
{
    advanceStateAggregate(Arguments::EndVariant);
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
    d->m_opsBase = d->m_opsPtr;
    m_state = d->m_opsPtr->op.ioState;
    d->m_opsPtr++;

    if (uintptr_t(d->m_dataPtr) & 0x07) {
        m_state = Arguments::Finished;
        d->m_error.setCode(Error::DataNot8ByteAligned);
    }
}

template<ArgumentsBcReader::StateType stateType>
#ifdef __GNUC__
[[gnu::always_inline]]
#elif defined __MSVC__
[[msvc::forceinline]]
#endif
inline const void *ArgumentsBcReader::advanceStateInternal(Arguments::IoState expectedState)
{
    if (unlikely(m_state != expectedState)) { // nonrecoverable...
        d->mergeError(Error::ReadWrongType);
        return s_nullBuffer;
    }

    // Normally (in release builds), no need for a length check on ops because ops are pre-validated and
    // FerOps::End marks the end.
    assert(d->m_opsBase == &(*d->m_ops)[0]);
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

    if constexpr (stateType == StateType::Primitive) {
        switch (ferOp.op) {
        case FerOpcode::Copy0:
            break;
        case FerOpcode::Copy1:
        case FerOpcode::Copy2:
        case FerOpcode::Copy4:
        case FerOpcode::Copy8:
            newPtr += 1 << uint32(ferOp.op);
            break;
        default:
            unreachable();
            break;
        }
    } else if constexpr (stateType == StateType::String) {
        switch (ferOp.op) {
        case FerOpcode::String: {
            // ### maybe do content validation after alignment to next element and data length check to avoid
            // length-checking twice. But we do need to check max string length for String and ObjectPath!
            newPtr += sizeof(uint32);
            VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
            uint32 len = *reinterpret_cast<const uint32 *>(ret);
            VALID_IF(Arguments::isStringValid(cstring(const_cast<byte*>(newPtr), len), d->m_validateUtf8),
                    Error::InvalidString);
            newPtr += len + 1 /* trailing nul */;
            break; }
        case FerOpcode::ObjectPath: {
            newPtr += sizeof(uint32);
            VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
            uint32 len = *reinterpret_cast<const uint32 *>(ret);
            VALID_IF(Arguments::isObjectPathValid(cstring(const_cast<byte*>(newPtr), len)),
                    Error::InvalidObjectPath);
            newPtr += len + 1 /* trailing nul */;
            break; }
        case FerOpcode::Signature: {
            newPtr += sizeof(byte);
            VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
            byte len = *reinterpret_cast<const byte *>(ret);
            VALID_IF(Arguments::isSignatureValid(cstring(const_cast<byte*>(newPtr), len)),
                    Error::InvalidSignature);
            newPtr += len + 1 /* trailing nul */;
            break; }
        default:
            unreachable();
            break;
        }
    } else {
        switch (ferOp.op) {
        case FerOpcode::EnterVariant: {
            const FerNesting outerNest = d->m_opsPtr->nest;
            d->m_nesting.array += outerNest.arrayDepth;
            d->m_nesting.paren += outerNest.parenDepth;
            d->m_nesting.variant += 1;

            const uint32 opsIndex = d->m_opsPtr + 1 - d->m_opsBase; // point it after the EnterVariant + FerNest
            d->m_variantStack.emplace_back(VariantInfo{std::move(d->m_ops), opsIndex});

            const byte len = *reinterpret_cast<const byte *>(ret);
            const cstring newSig(const_cast<byte *>(ret + sizeof(byte)), len);
            newPtr += sizeof(byte);
            newPtr += len + 1 /* trailing nul */;

            d->m_ops = ferCodeForSignature(newSig, Arguments::VariantSignature);
            assert(d->m_ops->size() >= 4); // two begin, one end, >= 1 data elements  // TODO error handling
            d->m_opsPtr = &(*d->m_ops)[0];
            d->m_opsBase = d->m_opsPtr;

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
            d->m_opsBase = &(*d->m_ops)[0];
            d->m_opsPtr = d->m_opsBase + varInfo.prevOpsIndex;
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

            d->m_opsPtr++; // skip FerRepeatArray
            d->m_dataEnd = d->m_arrayLengthStack.back();
            d->m_arrayLengthStack.pop_back();
            break; }

            // The following two should only occur at the beginning of a signature, which we don't handle here
        case FerOpcode::BeginVariantSignature:
        case FerOpcode::BeginMethodSignature:
            // The following are handled by their own begin*() methods
        case FerOpcode::BeginArray:
        default:
            unreachable();
            break;
        }
    }

    uint32_t postAlignExponent = ferOp.postAlignExponent;

    // EndArray is handled by "looking ahead" from the last element in the array so that API clients don't
    // need to call anything like nextArrayElement() to immediately read the next element.
    // Probably not great for performance!
    if (ferOp.isLastOfArray) {
        if (newPtr < d->m_dataEnd) {
            const FerRepeatArray repeatArrayData = (d->m_opsPtr + 1)->repeatArray;

            // "another round"
            d->m_opsPtr = d->m_opsBase + repeatArrayData.goBackOpIndex;
            assert((d->m_opsPtr - 1)->op.op == FerOpcode::BeginArray);
            m_state = (d->m_opsPtr - 1)->op.ioState;
            postAlignExponent = repeatArrayData.goBackAlignExponent;
        } else {
            // (Normally, newPtr == d->m_dataEnd here, but if it's greater, that's Ok too: the error
            // will be handled shortly by the VALID_IFs below)
            // Note: any non-zero postAlignExponent for the array is packaged with the EndErray opcode,
            // not this one! That is for the best because m_dataEnd is still the end of the array here,
            // not of all data, so advancing the data pointer through alignment would produce a (usually)
            // spurious out of bounds error.
            assert(postAlignExponent == 0);
            if (postAlignExponent) {
                unreachable();
            }
            m_state = ferOp.ioState;
        }
    } else {
        m_state = ferOp.ioState;
    }

    if (!postAlignExponent) {
        VALID_IF(newPtr <= d->m_dataEnd, Error::MalformedMessageData);
    } else {
        const byte *unalignedNewPtr = newPtr;
        newPtr = align(newPtr, 1 << postAlignExponent);
        VALID_IF(newPtr <= d->m_dataEnd && isPaddingZero(unalignedNewPtr, newPtr),
                 Error::MalformedMessageData);
    }

    d->m_dataPtr = newPtr;
    return ret;
}

const void *ArgumentsBcReader::advanceStatePrimitive(Arguments::IoState expectedState)
{
    return advanceStateInternal<StateType::Primitive>(expectedState);
}

const void *ArgumentsBcReader::advanceStateString(Arguments::IoState expectedState)
{
    return advanceStateInternal<StateType::String>(expectedState);
}

const void *ArgumentsBcReader::advanceStateAggregate(Arguments::IoState expectedState)
{
    return advanceStateInternal<StateType::Aggregate>(expectedState);
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
    const uint32 index = *reinterpret_cast<const uint32 *>(advanceStatePrimitive(Arguments::UnixFd));
    if (index < d->m_fileDescriptors.size()) {
        return d->m_fileDescriptors[index];
    }
    return InvalidFileDescriptor;
}
