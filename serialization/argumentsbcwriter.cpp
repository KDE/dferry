// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "argumentsbcwriter.h"

#include "arguments_p.h"

#include "message.h"
#include "fercode_p.h"
#include "types.h"

#include <cstdlib>

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

#undef VALID_IF
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    m_state = Arguments::Finished; d->mergeError(errCode); return; }


static inline byte *align(byte *index, uintptr_t alignment)
{
    // it also works with 1, but makes no sense
    if (alignment < 2 || alignment > 8) {
        unreachable();
    }
    const uintptr_t maxStepUp = alignment - 1;
    return reinterpret_cast<byte *>(uintptr_t(index + maxStepUp) & ~maxStepUp);
}

static inline byte *zeroPad(byte *data, uint32 alignment)
{
    if (alignment < 2 || alignment > 8) {
        unreachable();
    }
    byte *padEnd = align(data, alignment);
    for (; data < padEnd; data++) {
        *data = '\0';
    }
    return padEnd;
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

class ArgumentsBcWriter::Private
{
public:
    const FerCode *m_opsPtr{};    // end pointer not needed, we stop at FerOpcode::End
    byte *m_dataPtr{};
    const byte *m_dataEnd{};
    byte *m_dataPtrBeforeLoopBack{};
    const FerCode *m_opsBase{};
    byte *m_dataBegin{};
    Nesting m_nesting;
    uint32 m_signatureLength{};
    Error m_error;
    bool m_validateUtf8 = true;

    struct ArrayInfo
    {
        uint32 lengthFieldOffset;
        uint16 containedTypeBegin; // allows two things:
                                   // - determine whether the "signature cursor" is at a position that allows endArray()
                                   // - re-derive the (aligned) data start offset
        bool isDict;
    };

#ifdef HAVE_BOOST
    boost::local_shared_ptr<std::vector<FerCode>> m_ops;
    boost::container::small_vector<ArrayInfo, 8> m_arrayStack;
#else
    std::shared_ptr<std::vector<FerCode>> m_ops;
    std::vector<ArrayInfo> m_arrayStack;
#endif
    std::vector<VariantInfo> m_variantStack;
    std::vector<int> m_fileDescriptors;

    void mergeError(Error::Code errorCode);

    bool reserveData(uint32 size)
    {
        size += 16; // enough extra for anything but variable-length data such as strings and arrays
        if (likely(m_dataPtr + size <= m_dataEnd)) {
            return true;
        }

        const uintptr_t dataPos = uintptr_t(m_dataPtr - m_dataBegin);
        uint32 newCapacity = m_dataEnd - m_dataBegin;
        do {
            newCapacity *= 2;
        } while (size > newCapacity);

        m_dataBegin = reinterpret_cast<byte *>(realloc(m_dataBegin, newCapacity));
        m_dataEnd = m_dataBegin + newCapacity;
        m_dataPtr = m_dataBegin + dataPos;

        // Here, we trade off getting an ArgumentsTooLong error as early as possible for fewer
        // conditional branches in the hot path. Because only the final message length has a well-defined
        // limit, and Arguments doesn't, a precise check has limited usefulness anwyay. This is just
        // a sanity check and out of bounds access / overflow protection.

        // In most cases, callers do not need to check for errors: Very large single arguments are
        // already rejected, and we actually allocate the too large buffer to prevent out-of-bounds
        // access. Any following writing API calls will then cleanly abort due to m_state == Finished.
        if (unlikely(!m_dataBegin || newCapacity > Arguments::MaxMessageLength * 3)) {
            m_dataPtr = nullptr;
            return false;
        }
        return true;
    }
};

void ArgumentsBcWriter::Private::mergeError(Error::Code errorCode)
{
    if (!m_error.isError()) {
        m_error.setCode(errorCode);
    }
}

/** \class ArgumentsBcWriter
    Writes arguments into Arguments.

    This is a single use factory class: The last step in its use is to move out the finished
    Arguments instance.

    This class is less flexible, but generally about 2.5-3.5x as fast as ArgumentsWriter if the bytecode
    already exists.

    \see Arguments
*/

/// Constructs an %ArgumentsBcWriter to read arguments from \p args. A preprocessed type signature according
/// to \p encodeOptions is taken from a thread-local cache or created if necessery.
ArgumentsBcWriter::ArgumentsBcWriter(cstring signature, Arguments::FerEncodeOptions encodeOptions)
    : d(new Private)
{
    d->m_ops = ferCodeForSignature(signature, Arguments::MethodSignature, encodeOptions);
    beginWrite(signature);
}

/// Constructs an %ArgumentsBcWriter to read arguments from \p args, using a prep-processed type signature
/// in \p ferCode.
#ifdef HAVE_BOOST
ArgumentsBcWriter::ArgumentsBcWriter(cstring signature, boost::local_shared_ptr<std::vector<FerCode>> ferCode)
#else
ArgumentsBcWriter::ArgumentsBcWriter(cstring signature, std::shared_ptr<std::vector<FerCode>> ferCode)
#endif
    : d(new Private)
{
    d->m_ops = ferCode;
    beginWrite(signature);
}

/// Takes ownership of \p other's data and makes \p other invalid.
ArgumentsBcWriter::ArgumentsBcWriter(ArgumentsBcWriter &&other)
    : m_state(other.m_state),
      d(other.d)
{
    other.d = nullptr;
}

/// Copies \p other with all of its state, including read position.
ArgumentsBcWriter::ArgumentsBcWriter(const ArgumentsBcWriter &other)
    : m_state(other.m_state),
      d(nullptr)

{
    if (other.d) {
        d = new Private(*other.d);
    }
}

/// Takes ownership of \p other's data and makes \p other invalid.
void ArgumentsBcWriter::operator=(ArgumentsBcWriter &&other)
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
void ArgumentsBcWriter::operator=(const ArgumentsBcWriter &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    if (d && other.d) {
        *d = *other.d;
    } else {
        ArgumentsBcWriter temp(other);
        std::swap(d, temp.d);
    }
}

ArgumentsBcWriter::~ArgumentsBcWriter()
{
    delete d;
    d = nullptr;
}

/// \fn Arguments::IoState ArgumentsBcWriter::state() const
/// \returns the type of the next element. In case of error or end of data, the return
///          value will indicate that instead.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \returns the current state() as a string
cstring ArgumentsBcWriter::stateString() const
{
    return printableState(m_state);
}

/// \returns the error that occured on this reader, if any. Otherwise, the return value has error
///          code Error::NoError.
Error ArgumentsBcWriter::error() const
{
    return d->m_error;
}

/// \returns wnether the reader is in an error state. Error states are not recoverable.
bool ArgumentsBcWriter::isError() const
{
    return d->m_error.isError();
}

/// \returns wnether the reader is in a final state, may be success or error.
/// \see isError()
bool ArgumentsBcWriter::isFinished() const
{
    return m_state == Arguments::Finished;
}

/// \returns isFinished() && !isError()
bool ArgumentsBcWriter::isSuccess() const
{
    return m_state == Arguments::Finished && !d->m_error.isError();
}

/// Sets whether the reader fully validates the UTF-8 encoding of strings.
/// \see validatesUtf8
void ArgumentsBcWriter::setValidatesUtf8(bool validate)
{
    d->m_validateUtf8 = validate;
}

/// \returns whether the reader fully validates the UTF-8 encoding of strings.
/// The reader always validates that strings do note exceeed maximum allowed Length and have a null
/// terminator. This property controls whether string are also checked to be valid UTF-8 and not
/// contain null bytes before the end. The default value is \c true.
/// Disabling UTF-8 validation is faster, but insecure if the sender is not trusted.
/// \see setValidatesUtf8
bool ArgumentsBcWriter::validatesUtf8() const
{
    return d->m_validateUtf8;
}

void ArgumentsBcWriter::beginArrayInternal(bool isDict)
{
    VALID_IF(d->reserveData(0), Error::ArgumentsTooLong);

    FerOp ferOp = d->m_opsPtr->op;
    d->m_opsPtr++;

    d->m_arrayStack.push_back({uint32(d->m_dataPtr - d->m_dataBegin),
                               uint16(d->m_opsPtr - d->m_opsBase), isDict});

    byte* newPtr = d->m_dataPtr + sizeof(uint32);

    // Always align newPtr to array contents. It's a quirk of DBus serialization that even
    // zero elements are aligned.
    if (ferOp.postAlign) {
        assert(ferOp.postAlign == 8); // we're already 4-aligned at / after the length field
        newPtr = zeroPad(newPtr, 8);
    }
    d->m_dataPtr = newPtr;
    m_state = ferOp.ioState;
}

/// Starts writing an array.
void ArgumentsBcWriter::beginArray()
{
    VALID_IF(m_state == Arguments::BeginArray, Error::ReadWrongType); // TODO WriteWrongType?
    beginArrayInternal(false);
}

void ArgumentsBcWriter::endArrayInternal(bool isDict)
{
    VALID_IF(!d->m_arrayStack.empty(), Error::ReadWrongType);
    const ArgumentsBcWriter::Private::ArrayInfo arrayInfo = d->m_arrayStack.back();
    d->m_arrayStack.pop_back();
    VALID_IF(arrayInfo.isDict == isDict, Error::ReadWrongType); // TODO WriteWrongType?

    const FerCode *expectedOpPosition = d->m_opsBase + arrayInfo.containedTypeBegin;
    VALID_IF(expectedOpPosition == d->m_opsPtr, Error::ReadWrongType); // TODO WriteWrongType?

    byte *lengthField = d->m_dataBegin + arrayInfo.lengthFieldOffset;
    byte *contentsStart = lengthField + sizeof(uint32);
    const uint32 contentsAlign = (d->m_opsPtr - 1)->op.postAlign;
    if (contentsAlign) {
        contentsStart = align(contentsStart, contentsAlign);
    }

    uint32 arrayLength = 0;
    if (d->m_dataPtr > contentsStart) {
        arrayLength = d->m_dataPtrBeforeLoopBack - contentsStart;
        // undo alignment padding to next array item (there isn't one)
        d->m_dataPtr = d->m_dataPtrBeforeLoopBack;
    }

    VALID_IF(arrayLength <= Arguments::MaxArrayLength, Error::ArrayOrDictTooLong);
    *reinterpret_cast<uint32 *>(lengthField) = arrayLength;

    m_state = isDict ? Arguments::EndDict : Arguments::EndArray;

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
                advanceStateAggregate(m_state);
                return;
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
    return; // just in case
}

/// Ends writing an array.
/// The required state is right after BeginArray or at the end of a content item.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcWriter::endArray()
{
    endArrayInternal(false);
}

/// Starts writing a dict.
/// \returns true if the dict has >= 1 elements, false if it has 0.
void ArgumentsBcWriter::beginDict()
{
    VALID_IF(m_state == Arguments::BeginDict, Error::ReadWrongType); // TODO WriteWrongType?
    beginArrayInternal(true);
}

/// Ends writing a dict.
/// The required state is right after BeginDict or at the end of a content item.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcWriter::endDict()
{
    endArrayInternal(true);
}

/// \fn void ArgumentsBcWriter::beginStruct()
/// Begins writing a struct.
/// Required state: Arguments::BeginStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::endStruct()
/// Ends writing a struct.
/// Required state: Arguments::EndStruct.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// Begins writing a variant.
/// Required state:  Arguments::BeginVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcWriter::beginVariant()
{
    advanceStateAggregate(Arguments::BeginVariant);
}

/// Ends writing a variant.
/// Required state: Arguments::EndVariant.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcWriter::endVariant()
{
    advanceStateAggregate(Arguments::EndVariant);
}

#ifdef WITH_DICT_ENTRY
void ArgumentsBcWriter::beginDictEntry()
{
}

void ArgumentsBcWriter::endDictEntry()
{
}
#endif

void ArgumentsBcWriter::beginWrite(cstring signature)
{
    d->m_opsPtr = &(*d->m_ops)[0];
    d->m_opsBase = d->m_opsPtr;
    m_state = d->m_opsPtr->op.ioState;
    d->m_opsPtr++;

    d->m_dataPtr = static_cast<byte *>(malloc(512));
    d->m_dataEnd = d->m_dataPtr + 512;
    d->m_dataBegin = d->m_dataPtr;
    if (uintptr_t(d->m_dataPtr) & 0x07) { // this should *never* happen with a reasonable malloc
        m_state = Arguments::Finished;
        d->m_error.setCode(Error::DataNot8ByteAligned);
    }

    if (signature.length) {
        memcpy(d->m_dataBegin, signature.ptr, signature.length);
        d->m_signatureLength = signature.length;
        d->m_dataPtr += signature.length;
        *d->m_dataPtr = '\0';
        // serialized data must start 8-aligned according to spec (and we internally rely on that to
        // keep all multi-byte types naturally aligned)
        d->m_dataPtr = zeroPad(d->m_dataPtr + 1, 8);
    }
}

// Ideas to *maybe* improve performance:
// - Add something to FerCode to reserveData() for runs of fixed-size data, similar to the
//   consolidated length checks in codegen
// - Do alignment before writing a piece of data, not after the previous one. The current system
//   seems to work quite well for reading, but maybe the other way around is better for writing.

template<ArgumentsBcWriter::StateType stateType>
#ifdef __GNUC__
[[gnu::always_inline]]
#elif defined __MSVC__
[[msvc::forceinline]]
#endif
inline void ArgumentsBcWriter::advanceStateInternal(Arguments::IoState expectedState, uint64 arg, uint32 arg2)
{
    if (unlikely(m_state != expectedState)) { // nonrecoverable...
        d->mergeError(Error::ReadWrongType); // TODO name!
        return;
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

    byte *newPtr = d->m_dataPtr;

    if constexpr (stateType == StateType::Primitive) {
        VALID_IF(d->reserveData(0), Error::ArgumentsTooLong);
        newPtr = d->m_dataPtr;

        switch (ferOp.op) {
        case FerOpcode::Copy1:
            *reinterpret_cast<byte *>(newPtr) = static_cast<byte>(arg);
            newPtr += 1;
            break;
        case FerOpcode::Copy2:
            *reinterpret_cast<uint16 *>(newPtr) = static_cast<uint16>(arg);
            newPtr += 2;
            break;
        case FerOpcode::Copy4:
            *reinterpret_cast<uint32 *>(newPtr) = static_cast<uint32>(arg);
            newPtr += 4;
            break;
        case FerOpcode::Copy8:
            *reinterpret_cast<uint64 *>(newPtr) = arg;
            newPtr += 8;
            break;
        default:
            unreachable();
            break;
        }

    } else if constexpr (stateType == StateType::String) {
        const cstring str(reinterpret_cast<byte *>(arg), arg2);
        VALID_IF(d->reserveData(str.length), Error::ArgumentsTooLong);
        newPtr = d->m_dataPtr;

        switch (ferOp.op) {
        case FerOpcode::String: {
            VALID_IF(Arguments::isStringValid(str, d->m_validateUtf8),
                     Error::InvalidString);
            *reinterpret_cast<uint32 *>(newPtr) = str.length;
            newPtr += sizeof(uint32);
            break; }
        case FerOpcode::ObjectPath: {
            VALID_IF(Arguments::isObjectPathValid(cstring(reinterpret_cast<byte *>(arg), arg2)),
                     Error::InvalidObjectPath);
            *reinterpret_cast<uint32 *>(newPtr) = str.length;
            newPtr += sizeof(uint32);
            break; }
        case FerOpcode::Signature: {
            VALID_IF(Arguments::isSignatureValid(cstring(reinterpret_cast<byte *>(arg), arg2)),
                     Error::InvalidSignature);
            *reinterpret_cast<byte *>(newPtr) = str.length;
            newPtr += sizeof(byte);
            break; }
        default:
            unreachable();
            break;
        }

        memcpy(newPtr, str.ptr, str.length + /* trailing nul */ 1);
        newPtr += str.length + 1;
    } else {
        switch (ferOp.op) {
        case FerOpcode::EnterVariant: {
            // TODO!! this will have to work differently from other aggregates: the user needs to pass in
            // the signature (and bytecode, for performance, if available) of the contained type.
            // Also: reserve data for signature before writing it!

            const FerNesting outerNest = d->m_opsPtr->nest;
            d->m_nesting.array += outerNest.arrayDepth;
            d->m_nesting.paren += outerNest.parenDepth;
            d->m_nesting.variant += 1;

            const uint32 opsIndex = d->m_opsPtr + 1 - d->m_opsBase; // point it after the EnterVariant + FerNest
            d->m_variantStack.emplace_back(VariantInfo{std::move(d->m_ops), opsIndex});

#if 0 // FIXME
            const byte len = *reinterpret_cast<const byte *>(ret);
            const cstring newSig(const_cast<byte *>(ret + sizeof(byte)), len);
            newPtr += sizeof(byte);
            newPtr += len + 1 /* trailing nul */;

            d->m_ops = ferCodeForSignature(newSig, Arguments::VariantSignature);
            assert(d->m_ops->size() >= 4); // two begin, one end, >= 1 data elements  // TODO error handling
            d->m_opsPtr = &(*d->m_ops)[0];
            d->m_opsBase = d->m_opsPtr;

            ferOp = d->m_opsPtr->op;
#endif
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
            assert(ferOp.postAlign == 0); // alignment for next element at the end never makes sense

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
            ferOp.postAlign = enterVariantOp.postAlign;
            // avoid bit fiddling by copying all bits in the first byte of FerOp - it makes no other difference
            ferOp.op = enterVariantOp.op;

            break; }

        case FerOpcode::End:
            assert(ferOp.postAlign == 0); // alignment for next element at the end never makes sense
            assert(d->m_arrayStack.empty());
            assert(d->m_variantStack.empty());
            assert(!d->m_nesting.array);
            assert(!d->m_nesting.paren);
            assert(!d->m_nesting.variant);
            m_state = Arguments::Finished;
            return;

        case FerOpcode::EndArray:
            d->m_opsPtr++; // skip FerRepeatArray
            break;

        case FerOpcode::Copy0: // BeginStruct / EndStruct
            break;

            // The following two should only occur at the beginning of a signature, which we don't handle here
        case FerOpcode::BeginVariantSignature:
        case FerOpcode::BeginMethodSignature:
            // The following are handled by their own begin*() or end*() methods
        case FerOpcode::BeginArray:
        default:
            unreachable();
            assert(false);
            break;
        }
    }

    uint32_t postAlign = ferOp.postAlign;

    // End-of-array handling is a bit screwy and probably has to be if we don't want to ask clients to call
    // nextArrayItem() or something before writing the next item. We need to go to the first element and
    // prepare everything to just go on writing without any special calls, then undo that when the client
    // calls endArray() instead.
    if (ferOp.isLastOfArray) {
        const FerRepeatArray repeatArrayData = (d->m_opsPtr + 1)->repeatArray;

        // "another round"
        d->m_opsPtr = d->m_opsBase + repeatArrayData.goBackOpIndex;
        m_state = (d->m_opsPtr - 1)->op.ioState;
        postAlign = repeatArrayData.goBackAlign;
        d->m_dataPtrBeforeLoopBack = newPtr;
    } else {
        m_state = ferOp.ioState;
    }

    if (postAlign) {
        newPtr = zeroPad(newPtr, postAlign);
    }

    d->m_dataPtr = newPtr;
    return;
}

void ArgumentsBcWriter::advanceStatePrimitive(Arguments::IoState expectedState, uint64 arg)
{
    advanceStateInternal<StateType::Primitive>(expectedState, arg, 0);
}

void ArgumentsBcWriter::advanceStateString(Arguments::IoState expectedState, cstring arg)
{
    advanceStateInternal<StateType::String>(expectedState, reinterpret_cast<uint64>(arg.ptr), arg.length);
}

void ArgumentsBcWriter::advanceStateAggregate(Arguments::IoState expectedState)
{
    advanceStateInternal<StateType::Aggregate>(expectedState, 0, 0);
}


/// Finishes writing and returns an Arguments with the data that has been written.
/// If there was an error, the returned Arguments will contain an error and no data.
Arguments ArgumentsBcWriter::finish()
{
    Arguments args;
    Arguments::Private *argsPriv = Arguments::Private::of(&args);

    if (m_state != Arguments::Finished) {
        d->mergeError(Error::CannotEndArgumentsWithOpenAggregates); // TODO error is not quite correct
    }

    argsPriv->m_error = d->m_error;
    if (!d->m_error.isError()) {
        // having no error and a signature implies having data
        if (d->m_signatureLength) {
            byte* const dataStart = align(d->m_dataBegin + d->m_signatureLength + 1, 8);
            const size_t dataSize = d->m_dataPtr - dataStart;

            if (dataSize > Arguments::MaxMessageLength) {
                d->mergeError(Error::ArgumentsTooLong);
                argsPriv->m_error = d->m_error;
            } else {
                argsPriv->m_memOwnership = d->m_dataBegin;
                argsPriv->m_signature = cstring(d->m_dataBegin, d->m_signatureLength);
                argsPriv->m_data = chunk(dataStart, dataSize);

                d->m_dataPtr = nullptr; // now owned by Arguments and later freed there
                d->m_dataEnd = nullptr;
                d->m_dataBegin = nullptr;

                argsPriv->m_fileDescriptors = std::move(d->m_fileDescriptors);
            }
        }
    }

    return args;
}

/// \fn void ArgumentsBcWriter::writeByte(byte arg)
/// Writes a byte (8 bit unsigned int). Required state: Arguments::Byte.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeBoolean(bool arg)
/// Writes a boolean value. Required state: Arguments::Boolean.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeInt16(int16 arg)
/// Writes a 16 bit signed int value. Required state: Arguments::Int16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeUint16(uint16 arg)
/// Writes a 16 bit unsigned int value. Required state: Arguments::Uint16.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeInt32(int32 arg)
/// Writes a 32 bit signed int value. Required state: Arguments::Int32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeUint32(uint32 arg)
/// Writes a 32 bit unsigned int value. Required state: Arguments::Uint32.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeInt64(int64 arg)
/// Writes a 64 bit signed int value. Required state: Arguments::Int64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeUint64(uint64 arg)
/// Writes a 64 bit unsigned int value. Required state: Arguments::Uint64.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeDouble(double arg)
/// Writes a double-precision floating point value. Required state: Arguments::Double.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeString(cstring arg)
/// Writes a string. Required state: Arguments::String.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeObjectPath(cstring arg)
/// Writes a DBus object path. Required state: Arguments::ObjectPath.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// \fn void ArgumentsBcWriter::writeSignature(cstring arg)
/// Writes a DBus type signature. Required state: Arguments::Signature.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"

/// Writes a file descriptor. The numeric value will usually not be the same as on the sending
/// side, but it will refer to the same file. Required state: Arguments::UnixFd.
/// \see \ref argsreader_concepts "ArgumentsReader Concepts"
void ArgumentsBcWriter::writeUnixFd(int arg)
{
    const uint32 index = d->m_fileDescriptors.size();
    d->m_fileDescriptors.push_back(arg);
    advanceStatePrimitive(Arguments::UnixFd, index);
}
