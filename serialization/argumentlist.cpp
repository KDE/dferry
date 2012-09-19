#include "argumentlist.h"

#include "basictypeio.h"

#include <cassert>

static int align(uint32 index, uint32 alignment)
{
    const int maxStepUp = alignment - 1;
    return (index + maxStepUp) & ~maxStepUp;
}

// helper to verify the max nesting requirements of the d-bus spec
struct Nesting
{
    Nesting() : array(0), paren(0), variant(0) {}
    static const int arrayMax = 32;
    static const int parenMax = 32;
    static const int totalMax = 64;

    bool beginArray() { array++; return array <= arrayMax && total() <= totalMax; }
    void endArray() { array--; }
    bool beginParen() { paren++; return paren <= parenMax && total() <= totalMax; }
    void endParen() { paren--; }
    bool beginVariant() { variant++; return total() <= totalMax; }
    void endVariant() { variant--; }
    int total() { return array + paren + variant; }

    int array;
    int paren;
    int variant;
};

ArgumentList::ArgumentList()
   : m_isByteSwapped(false),
     m_readCursorCount(0),
     m_writeCursor(0)
{
}

ArgumentList::ArgumentList(array signature, array data, bool isByteSwapped)
   : m_isByteSwapped(isByteSwapped),
     m_readCursorCount(0),
     m_writeCursor(0),
     m_signature(signature),
     m_data(data)
{
}

static void chopFirst(array *a)
{
    a->begin++;
    a->length--;
}

static bool parseBasicType(array *a)
{
    assert(a->length >= 0);
    if (a->length < 1) {
        return false;
    }
    switch (*a->begin) {
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
    case 'h': // this doesn't seem to make sense, though...
        chopFirst(a);
        return true;
    default:
        return false;
    }
}

static bool parseSingleCompleteType(array *a, Nesting *nest)
{
    assert(a->length >= 0);
    if (a->length < 1) {
        return false;
    }

    switch (*a->begin) {
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
    case 'h':
        chopFirst(a);
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }
        chopFirst(a);
        nest->endVariant();
        return true;
    case '(': {
        if (!nest->beginParen()) {
            return false;
        }
        chopFirst(a);
        bool isEmptyStruct = true;
        while (parseSingleCompleteType(a, nest)) {
            isEmptyStruct = false;
        }
        if (!a->length || *a->begin != ')' || isEmptyStruct) {
            return false;
        }
        chopFirst(a);
        nest->endParen();
        return true; }
    case 'a':
        if (!nest->beginArray()) {
            return false;
        }
        chopFirst(a);
        if (*a->begin == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || a->length < 4) {
                return false;
            }
            chopFirst(a);
            // key must be a basic type
            if (!parseBasicType(a)) {
                return false;
            }
            // value can be any type
            if (!parseSingleCompleteType(a, nest)) {
                return false;
            }
            if (!a->length || *a->begin != '}') {
                return false;
            }
            chopFirst(a);
            nest->endParen();
        } else { // regular array
            if (!parseSingleCompleteType(a, nest)) {
                return false;
            }
        }
        nest->endArray();
        return true;
    default:
        return false;
    }
}

//static
bool ArgumentList::isSignatureValid(array signature, bool isVariantSignature)
{
    Nesting nest;
    if (signature.length < 1 || signature.length > 256) {
        return false;
    }
    if (signature.begin[signature.length - 1] != 0) {
        return false; // not null-terminated
    }
    signature.length -= 1; // ignore the null-termination
    if (isVariantSignature) {
        if (signature.length && !parseSingleCompleteType(&signature, &nest)) {
            return false;
        }
        if (signature.length) {
            return false;
        }
    } else {
        while (signature.length) {
            if (!parseSingleCompleteType(&signature, &nest)) {
                return false;
            }
        }
    }
    // all aggregates must be closed at the end; if those asserts trigger the parsing code is not correct
    assert(!nest.array);
    assert(!nest.paren);
    assert(!nest.variant);
    return true;
}

ArgumentList::ReadCursor::ReadCursor(ArgumentList *al)
   : m_argList(al),
     m_state(NotStarted),
     m_nesting(new Nesting),
     m_signature(al->m_signature),
     m_data(al->m_data),
     m_signaturePosition(-1),
     m_dataPosition(0),
     m_zeroLengthArrayNesting(0)
{
    if (m_argList) {
        if (!ArgumentList::isSignatureValid(m_signature)) {
            m_state = InvalidData;
        }
        advanceState();
    }
}

ArgumentList::ReadCursor::~ReadCursor()
{
    if (m_argList) {
        m_argList->m_readCursorCount -= 1;
    }
    delete m_nesting;
    m_nesting = 0;
}

void ArgumentList::ReadCursor::replaceData(array data)
{
    m_data = data;
}

static void getTypeInfo(byte letterCode, ArgumentList::CursorState *beginState, uint32 *alignment,
                        bool *isPrimitiveType, bool *isStringType)
{
    ArgumentList::CursorState state = ArgumentList::InvalidData;
    bool isPrimitive = true;
    bool isString = false;
    int align = 4;
    // TODO use table lookup instead
    switch (letterCode) {
    case 'y':
        state = ArgumentList::Byte;
        align = 1;
        break;
    case 'b':
        state = ArgumentList::Boolean;
        break;
    case 'n':
        state = ArgumentList::Int16;
        align = 2;
        break;
    case 'q':
        state = ArgumentList::Uint16;
        align = 2;
        break;
    case 'i':
        state = ArgumentList::Int32;
        break;
    case 'u':
        state = ArgumentList::Uint32;
        break;
    case 'x':
        state = ArgumentList::Int64;
        align = 8;
        break;
    case 't':
        state = ArgumentList::Uint64;
        align = 8;
        break;
    case 'd':
        state = ArgumentList::Double;
        align = 8;
        break;
    case 's':
        state = ArgumentList::String;
        isPrimitive = false;
        isString = true;
        break;
    case 'o':
        state = ArgumentList::ObjectPath;
        isPrimitive = false;
        isString = true;
        break;
    case 'g':
        state = ArgumentList::Signature;
        isPrimitive = false;
        isString = true;
        align = 1;
        break;
    case 'h':
        state = ArgumentList::UnixFd;
        // this is handled like a primitive type with some extra postprocessing
        break;
    case 'v':
        state = ArgumentList::BeginVariant;
        isPrimitive = false;
        align = 1;
        break;
    case '(':
        state = ArgumentList::BeginStruct;
        isPrimitive = false;
        align = 8;
        break;
    case ')':
        state = ArgumentList::EndStruct;
        isPrimitive = false;
        align = 1;
        break;
    case 'a':
        state = ArgumentList::BeginArray;
        isPrimitive = false;
    case '{':
        state = ArgumentList::BeginDict;
        isPrimitive = false;
        align = 8;
        break;
    case '}':
        state = ArgumentList::EndDict;
        isPrimitive = false;
        align = 1;
        break;
    default:
        align = 1; // don't move the data read/write pointer by aligning it
        break;
    }
    if (beginState) {
        *beginState = state;
    }
    if (alignment) {
        *alignment = align;
    }
    if (isPrimitiveType) {
        *isPrimitiveType = isPrimitive;
    }
    if (isStringType) {
        *isStringType = isString;
    }
}

void ArgumentList::ReadCursor::advanceState()
{
    // TODO: when inside any array, we may never run out of data because the array length is given
    //       in the data stream and we won't start the array until its data is complete.
    // TODO: don't try to read any data when m_zeroLengthArrayNesting > 0; this needs special attention
    //       for arrays (automatically zero length) and variants (automatically empty signature)

    // if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData
    // we don't have to deal with invalid signatures here because they are checked beforehand EXCEPT
    // for aggregate nesting which cannot be checked using only one signature, due to variants.
    // variant signatures are only parsed while reading the data. individual variant signatures
    // ARE checked beforehand whenever we find one in this method.

    if (m_state == InvalidData) { // nonrecoverable...
        return;
    }

    assert(m_signaturePosition < m_signature.length);

    const int savedSignaturePosition = m_signaturePosition;
    const int savedDataPosition = m_dataPosition;

    // how to recognize end of...
    // - array entry: BeginArray at top of aggregate stack and we're not at the beginning
    //                of the array (so we've just finished reading a single complete type)
    // - dict entry: BeginDict at top of aggregate stack and the current type signature char is '}'
    // - array: at/past end of array data; end of array entry condition must hold, too
    // - dict: at/past end of dict data; end of dict entry conditions must hold, too
    // - struct: BeginStruct at top of aggregate stack and current char = ')'
    // - variant: BeginVariant at top of aggregate stack and at end of type signature
    // - argument list: aggregate stack is empty and at end of type signature

    // check if we are about to close any aggregate or even the whole argument list

    if (m_aggregateStack.empty()) {
        if (m_signaturePosition + 1 >= m_signature.length) {
            m_state = Finished;
            return;
        }
    } else {
        const AggregateInfo &aggregateInfo = m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginStruct:
            break; // already handled by getTypeInfo recognizing ')' -> EndStruct
        case BeginVariant:
            if (m_signaturePosition + 1 >= m_signature.length) {
                m_state = EndVariant;
                m_nesting->endVariant();
                m_signature.begin = aggregateInfo.var.prevSignature.begin;
                m_signature.length = aggregateInfo.var.prevSignature.length;
                m_signaturePosition = aggregateInfo.var.prevSignaturePosition;
                m_aggregateStack.pop_back();
                return;
            }
            break;
        case BeginDict:
            // fall through
        case BeginArray: {
            const bool isDict = aggregateInfo.aggregateType == BeginDict;
            const bool isEndOfEntry = isDict ? (m_signature.begin[m_signaturePosition + 1] == '}')
                                             : (m_signaturePosition > aggregateInfo.arr.containedTypeBegin);
            if (isEndOfEntry) {
                m_state = isDict ? NextDictEntry : NextArrayEntry;
                return; // the rest is handled in nextArrayOrDictEntry()
            } else {
                const bool isEndOfData = m_dataPosition >= aggregateInfo.arr.dataEndPosition;
                if (isEndOfData) {
                    m_state = InvalidData;
                    return;
                }
            }
            break; }
        default:
            break;
        }
    }

    // for aggregate types, it's just the alignment. for primitive types, it's also the actual size.
    uint32 requiredDataSize = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    m_signaturePosition++;
    getTypeInfo(m_signature.begin[m_signaturePosition],
                &m_state, &requiredDataSize, &isPrimitiveType, &isStringType);

    if (m_state == InvalidData) {
        return;
    }


    // check if we have enough data for the next type, and read it

    m_dataPosition = align(m_dataPosition, requiredDataSize);

    if (((isPrimitiveType || isStringType) && m_dataPosition + requiredDataSize > m_data.length)
        || m_dataPosition > m_data.length) {
        goto out_needMoreData;
    }

    // easy cases...
    if (isPrimitiveType) {
        switch(m_state) {
        case Byte:
            m_Byte = m_data.begin[m_dataPosition];
            break;
        case Boolean: {
            uint32 num = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            if (num > 1) {
                m_state = InvalidData;
            }
            m_Boolean = num == 1;
            break; }
        case Int16:
            m_Int16 = basic::readInt16(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Uint16:
            m_Uint16 = basic::readUint16(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Int32:
            m_Int32 = basic::readInt32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Uint32:
            m_Uint32 = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Int64:
            m_Int64 = basic::readInt64(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Uint64:
            m_Uint64 = basic::readUint64(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case Double:
            m_Double = basic::readDouble(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            break;
        case UnixFd: {
            uint32 index = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            uint32 ret; // TODO use index to retrieve the actual file descriptor
            m_Uint32 = ret;
            break; }
        default:
            assert(false);
            break;
        }
        m_dataPosition += requiredDataSize;
        return;
    }

    // strings
    if (isStringType) {
        uint32 stringLength = 1; // terminating nul
        if (m_state == Signature) {
            stringLength += m_data.begin[m_dataPosition];
        } else {
            stringLength += basic::readUint32(m_data.begin + m_dataPosition,
                                              m_argList->m_isByteSwapped);
        }
        m_dataPosition += requiredDataSize;
        if (m_dataPosition + stringLength > m_data.length) {
            goto out_needMoreData;
        }
        m_String.begin = m_data.begin + m_dataPosition;
        m_String.length = stringLength;
        m_dataPosition += stringLength;
        bool isValidString;
        if (m_state == String) {
            // TODO
        } else if (m_state == ObjectPath) {
            // TODO
        } else if (m_state == Signature) {
            isValidString = ArgumentList::isSignatureValid(array(m_String.begin, m_String.length));
        }
        if (!isValidString) {
            m_state = InvalidData;
        }
        return;
    }

    // now the interesting part: aggregates

    AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        if (!m_nesting->beginParen()) {
            m_state = InvalidData;
            return;
        }
        aggregateInfo.aggregateType = BeginStruct;
        m_aggregateStack.push_back(aggregateInfo);
        break;
    case EndStruct:
        m_nesting->endParen();
        if (!m_aggregateStack.size() || m_aggregateStack.back().aggregateType != BeginStruct) {
            assert(false); // should never happen due to the pre-validated signature
        }
        m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        if (m_dataPosition >= m_data.length) {
            goto out_needMoreData;
        }
        array signature;
        signature.length = m_data.begin[m_dataPosition++] + 1;
        signature.begin = m_data.begin + m_dataPosition;
        m_dataPosition += signature.length;
        if (m_dataPosition > m_data.length) {
            goto out_needMoreData;
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        if (!m_nesting->beginVariant()) {
            m_state = InvalidData;
            return;
        }

        if (!ArgumentList::isSignatureValid(signature, /* isVariantSignature = */ true)) {
            m_state = InvalidData;
            return;
        }

        aggregateInfo.aggregateType = BeginVariant;
        aggregateInfo.var.prevSignature.begin = m_signature.begin;
        aggregateInfo.var.prevSignature.length = m_signature.length;
        aggregateInfo.var.prevSignaturePosition = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);
        m_signature = signature;
        m_signaturePosition = -1; // because we increment m_signaturePosition before reading a char
        break; }

    case BeginArray: {
        if (m_dataPosition + 4 > m_data.length) {
            goto out_needMoreData;
        }
        static const int maxArrayDataLength = 67108864; // from the spec
        const uint32 arrayLength = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        if (arrayLength > maxArrayDataLength) {
            m_state = InvalidData;
            return;
        }
        m_dataPosition += 4;

        CursorState firstElementType;
        uint32 firstElementAlignment;
        getTypeInfo(m_signature.begin[m_signaturePosition + 1],
                    &firstElementType, &firstElementAlignment, 0, 0);

        m_state = firstElementType == BeginDict ? BeginDict : BeginArray;
        aggregateInfo.aggregateType = m_state;

        // TODO are we supposed to align m_dataPosition if the array is empty?
        m_dataPosition = align(m_dataPosition, firstElementAlignment);
        aggregateInfo.arr.dataEndPosition = m_dataPosition + arrayLength;
        if (aggregateInfo.arr.dataEndPosition > m_data.length) {
            // NB: do not clobber (the unsaved) nesting before potentially going to out_needMoreData!
            goto out_needMoreData;
        }
        if (!m_nesting->beginArray()) {
            m_state = InvalidData;
            return;
        }
        if (firstElementType == BeginDict) {
            m_signaturePosition++;
            if (!m_nesting->beginParen()) {
                m_state = InvalidData;
                return;
            }
        }
        // position at the 'a' or '{' because we increment m_signaturePosition before reading a char
        aggregateInfo.arr.containedTypeBegin = m_signaturePosition;
        aggregateInfo.arr.isZeroLength = !arrayLength;

        m_aggregateStack.push_back(aggregateInfo);
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    m_state = NeedMoreData;
    m_signaturePosition = savedSignaturePosition;
    m_dataPosition = savedDataPosition;
}

void ArgumentList::ReadCursor::advanceStateFrom(CursorState expectedState)
{
    if (m_state == expectedState) {
        advanceState();
    } else {
        m_state = InvalidData;
    }
}

// TODO introduce an error state different from InvalidData when the wrong method is called
void ArgumentList::ReadCursor::beginArray()
{
    if (m_state == BeginArray) {
        m_state = NextArrayEntry;
    } else {
        m_state = InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextArrayOrDictEntry(bool isDict, bool *outIsZeroLength)
{
    // cases to handle:
    // A zero-length array, first iteration, user does not want to look at types -> skip signature
    //   and clean up aggregate stack
    // B zero-length array, first iteration, user does want to look at types
    //   -> set it up, increase zero-length array nesting level
    // C zero-length array, second iteration -> clean up the aggregate stack and decrease zero-length
    //   array nesting level, thus possible leaving zero-length array mode
    // D nonzero-length array, not last iteration -> go back to start of signature
    // E nonzero-length array, last iteration -> ensure that data and signature both end,
    //   clean up aggregate stack

    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == BeginArray);

    if (outIsZeroLength) {
        *outIsZeroLength = aggregateInfo.arr.isZeroLength;
    }
    const bool isFirstIteration = m_signaturePosition <= aggregateInfo.arr.containedTypeBegin;
    if (aggregateInfo.arr.isZeroLength) {
        if (isFirstIteration) {
            if (outIsZeroLength) {
                // B
                m_zeroLengthArrayNesting++;
                return true; // TODO anything else?
            } else {
                // A
                // fix up nesting before we re-parse the beginning of the array signature
                array temp(m_signature.begin + m_signaturePosition, m_signature.length - m_signaturePosition);
                if (isDict) {
                    m_nesting->endParen();
                    m_signaturePosition--; // it was moved ahead by one to skip the '{'
                }
                m_nesting->endArray();
                if (!parseSingleCompleteType(&temp, m_nesting)) {
                    // must have been a nesting problem (assuming no bugs)
                    m_state = InvalidData;
                    return false;
                }
                m_signaturePosition = m_signature.length - temp.length - 1; // TODO check the indexing
            }
        } else {
            // C
            m_zeroLengthArrayNesting--;
        }
        // A and C
        m_state = isDict ? EndDict : EndArray;
        m_aggregateStack.pop_back();
        return false;
    } else {
        const bool isEndOfData = m_dataPosition >= aggregateInfo.arr.dataEndPosition;
        if (isEndOfData) {
            // E
            m_state = isDict ? EndDict : EndArray;
            if (isDict) {
                m_nesting->endParen();
                m_signaturePosition++; // skip '}'
            }
            m_nesting->endArray();
            m_aggregateStack.pop_back();
            return false;
        }
        // D
        // rewind to start of contained type and read the data there
        m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
        advanceState();
        return m_state != InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextArrayEntry(bool *outIsZeroLength)
{
    if (m_state == NextArrayEntry) {
        return nextArrayOrDictEntry(false, outIsZeroLength);
    } else {
        m_state = InvalidData;
        return false;
    }
}

void ArgumentList::ReadCursor::endArray()
{
    advanceStateFrom(EndArray);
}

bool ArgumentList::ReadCursor::beginDict()
{
    advanceStateFrom(BeginDict);
}

bool ArgumentList::ReadCursor::nextDictEntry(bool *outIsZeroLength)
{
    if (m_state == NextArrayEntry) {
        return nextArrayOrDictEntry(true, outIsZeroLength);
    } else {
        m_state = InvalidData;
        return false;
    }
}

bool ArgumentList::ReadCursor::endDict()
{
    advanceStateFrom(EndDict);
}

bool ArgumentList::ReadCursor::beginStruct()
{
    advanceStateFrom(BeginStruct);
}

bool ArgumentList::ReadCursor::endStruct()
{
    advanceStateFrom(EndStruct);
}

bool ArgumentList::ReadCursor::beginVariant()
{
    advanceStateFrom(BeginVariant);
}

bool ArgumentList::ReadCursor::endVariant()
{
    advanceStateFrom(EndVariant);
}

std::vector<ArgumentList::CursorState> ArgumentList::ReadCursor::aggregateStack() const
{
    const int count = m_aggregateStack.size();
    std::vector<CursorState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(m_aggregateStack[i].aggregateType);
    }
    return ret;
}

ArgumentList::WriteCursor::WriteCursor(ArgumentList *al)
   : m_argList(al),
     m_signaturePosition(0),
     m_dataPosition(0)
{
}

ArgumentList::WriteCursor::~WriteCursor()
{
    if (m_argList) {
        assert(m_argList->m_writeCursor);
        m_argList->m_writeCursor = 0;
    }
}
