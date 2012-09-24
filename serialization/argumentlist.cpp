#include "argumentlist.h"

#include "basictypeio.h"

#include <cassert>
#include <cstring>

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

// static
bool ArgumentList::isStringValid(array string)
{
    if (!string.length || string.begin[string.length - 1] != 0) {
        return false;
    }
    for (int i = 0; i < string.length - 1; i++) {
        if (string.begin[i] == 0) {
            return false;
        }
    }
    return true;
}

static bool isObjectNameLetter(byte b)
{
    return (b >= 'a' && b <= 'z') || b == '_' || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9');
}

// static
bool ArgumentList::isObjectPathValid(array string)
{
    if (string.length < 2 || string.begin[string.length - 1] != 0) {
        return false;
    }
    byte lastLetter = string.begin[0];
    if (lastLetter != '/') {
        return false;
    }
    if (string.length == 2) {
        return true; // "/\0" special case
    }
    for (int i = 1; i < string.length; i++) {
        byte currentLetter = string.begin[i];
        if (lastLetter == '/') {
            if (!isObjectNameLetter(currentLetter)) {
                return false;
            }
        } else {
            if (currentLetter != '/' && !isObjectNameLetter(currentLetter)) {
                return false;
            }
        }
        lastLetter = currentLetter;
    }
    return lastLetter != '/';
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

ArgumentList::CursorState ArgumentList::ReadCursor::doReadPrimitiveType()
{
    switch(m_state) {
    case Byte:
        m_Byte = m_data.begin[m_dataPosition];
        break;
    case Boolean: {
        uint32 num = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
        m_Boolean = num == 1;
        if (num > 1) {
            return InvalidData;
        }
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
        return InvalidData;
    }
    return m_state;
}

ArgumentList::CursorState ArgumentList::ReadCursor::doReadString(int lengthPrefixSize)
{
    uint32 stringLength = 1; // terminating nul
    if (lengthPrefixSize == 1) {
        stringLength += m_data.begin[m_dataPosition];
    } else {
        stringLength += basic::readUint32(m_data.begin + m_dataPosition,
                                          m_argList->m_isByteSwapped);
    }
    m_dataPosition += lengthPrefixSize;
    if (m_dataPosition + stringLength > m_data.length) {
        return NeedMoreData;
    }
    m_String.begin = m_data.begin + m_dataPosition;
    m_String.length = stringLength;
    m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(array(m_String.begin, m_String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(array(m_String.begin, m_String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(array(m_String.begin, m_String.length));
    }
    if (!isValidString) {
        return InvalidData;
    }
    return m_state;
}

void ArgumentList::ReadCursor::advanceState()
{
    // TODO: when inside any array, we may never run out of data because the array length is given
    //       in the data stream and we won't start the array until its data is complete.

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
            break; // handled later by getTypeInfo recognizing ')' -> EndStruct
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
    // if we're in a zero-length array, we are iterating only over the types without reading any data

    if (m_zeroLengthArrayNesting && (isPrimitiveType || isStringType)) {
        return; // nothing to do
    }

    m_dataPosition = align(m_dataPosition, requiredDataSize);

    if (((isPrimitiveType || isStringType) && m_dataPosition + requiredDataSize > m_data.length)
        || m_dataPosition > m_data.length) {
        goto out_needMoreData;
    }

    if (isPrimitiveType) {
        m_state = doReadPrimitiveType();
        m_dataPosition += requiredDataSize;
        return;
    }

    if (isStringType) {
        m_state = doReadString(requiredDataSize);
        if (m_state == NeedMoreData) {
            goto out_needMoreData;
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
        if (m_zeroLengthArrayNesting) {
            static const char *emptyString = "";
            signature = array(const_cast<char *>(emptyString), 1);
        } else {
            signature.length = m_data.begin[m_dataPosition++] + 1;
            signature.begin = m_data.begin + m_dataPosition;
            m_dataPosition += signature.length;
            if (m_dataPosition > m_data.length) {
                goto out_needMoreData;
            }
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
        uint32 arrayLength = 0;
        if (!m_zeroLengthArrayNesting) {
            if (m_dataPosition + 4 > m_data.length) {
                goto out_needMoreData;
            }
            static const int maxArrayDataLength = 67108864; // from the spec
            arrayLength = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            if (arrayLength > maxArrayDataLength) {
                m_state = InvalidData;
                return;
            }
            m_dataPosition += 4;
        }

        CursorState firstElementType;
        uint32 firstElementAlignment;
        getTypeInfo(m_signature.begin[m_signaturePosition + 1],
                    &firstElementType, &firstElementAlignment, 0, 0);

        m_state = firstElementType == BeginDict ? BeginDict : BeginArray;
        aggregateInfo.aggregateType = m_state;

        // ### are we supposed to align m_dataPosition if the array is empty?
        if (!m_zeroLengthArrayNesting) {
            m_dataPosition = align(m_dataPosition, firstElementAlignment);
        }
        aggregateInfo.arr.dataEndPosition = m_dataPosition + arrayLength;
        if (aggregateInfo.arr.dataEndPosition > m_data.length) {
            // NB: do not clobber (the unsaved) nesting before potentially going to out_needMoreData!
            goto out_needMoreData;
        }
        bool nestOk = m_nesting->beginArray();
        if (firstElementType == BeginDict) {
            m_signaturePosition++;
            nestOk = nestOk && m_nesting->beginParen();
        }
        if (!nestOk) {
            m_state = InvalidData;
            return;
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
    if (m_nesting->array) {
        // we only start an array when the data for it has fully arrived (possible due to the length
        // prefix), so if we still run out of data in an array the input is inconsistent.
        m_state = InvalidData;
    }
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

void ArgumentList::ReadCursor::beginArrayOrDict(bool isDict, bool *isEmpty)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == BeginArray || aggregateInfo.aggregateType == BeginDict);

    if (isEmpty) {
        *isEmpty = aggregateInfo.arr.isZeroLength;
    }

    if (aggregateInfo.arr.isZeroLength) {
        m_zeroLengthArrayNesting++; // undone immediately in nextArrayOrDictEntry() when skipping
        if (!isEmpty) {
            // need to move m_signaturePosition to the end of the array signature or it won't happen
            array temp(m_signature.begin + m_signaturePosition, m_signature.length - m_signaturePosition);
            // fix up nesting before and after we re-parse the beginning of the array signature
            if (isDict) {
                m_nesting->endParen();
                m_signaturePosition--; // it was moved ahead by one to skip the '{'
            }
            m_nesting->endArray();
            if (!parseSingleCompleteType(&temp, m_nesting)) {
                // must have been too deep nesting (assuming no bugs)
                m_state = InvalidData;
                return;
            }
            m_nesting->beginArray();
            if (isDict) {
                m_nesting->beginParen();
            }
            m_signaturePosition = m_signature.length - temp.length - 1; // TODO check/fix the indexing
        }
    }
}

// TODO introduce an error state different from InvalidData when the wrong method is called
void ArgumentList::ReadCursor::beginArray(bool *isEmpty)
{
    if (m_state == BeginArray) {
        beginArrayOrDict(false, isEmpty);
    } else {
        m_state = InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextArrayOrDictEntry(bool isDict)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == BeginArray || aggregateInfo.aggregateType == BeginDict);

    if (aggregateInfo.arr.isZeroLength) {
        if (m_signaturePosition <= aggregateInfo.arr.containedTypeBegin) {
            // do one iteration to read the types
            return true;
        } else {
            // second iteration or skipping an empty array
            m_zeroLengthArrayNesting--;
        }
    } else {
        if (m_dataPosition < aggregateInfo.arr.dataEndPosition) {
            // rewind to start of contained type and read the data there
            m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
            advanceState();
            return m_state != InvalidData;
        }
    }
    // no more iterations
    m_state = isDict ? EndDict : EndArray;
    if (isDict) {
        m_nesting->endParen();
        m_signaturePosition++; // skip '}'
    }
    m_nesting->endArray();
    m_aggregateStack.pop_back();
    return false;
}

bool ArgumentList::ReadCursor::nextArrayEntry()
{
    if (m_state == NextArrayEntry) {
        return nextArrayOrDictEntry(false);
    } else {
        m_state = InvalidData;
        return false;
    }
}

void ArgumentList::ReadCursor::endArray()
{
    advanceStateFrom(EndArray);
}

bool ArgumentList::ReadCursor::beginDict(bool *isEmpty)
{
    if (m_state == BeginDict) {
        beginArrayOrDict(true, isEmpty);
    } else {
        m_state = InvalidData;
    }
}

bool ArgumentList::ReadCursor::nextDictEntry()
{
    if (m_state == NextDictEntry) {
        return nextArrayOrDictEntry(true);
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

ArgumentList::CursorState ArgumentList::WriteCursor::doWritePrimitiveType()
{
    // TODO: like, just rewrite from reading to writing
    switch(m_state) {
    case Byte:
        m_data.begin[m_dataPosition] = m_Byte;
        break;
    case Boolean: {
        uint32 num = m_Boolean ? 1 : 0;
        basic::writeUint32(m_data.begin + m_dataPosition, num);
        break; }
    case Int16:
        basic::writeInt16(m_data.begin + m_dataPosition, m_Int16);
        break;
    case Uint16:
        basic::writeUint16(m_data.begin + m_dataPosition, m_Uint16);
        break;
    case Int32:
        basic::writeInt32(m_data.begin + m_dataPosition, m_Int32);
        break;
    case Uint32:
        basic::writeUint32(m_data.begin + m_dataPosition, m_Uint32);
        break;
    case Int64:
        basic::writeInt64(m_data.begin + m_dataPosition, m_Int64);
        break;
    case Uint64:
        basic::writeUint64(m_data.begin + m_dataPosition, m_Uint64);
        break;
    case Double:
        basic::writeDouble(m_data.begin + m_dataPosition, m_Double);
        break;
    case UnixFd: {
        uint32 index; // TODO = index of the FD we actually want to send
        basic::writeUint32(m_data.begin + m_dataPosition, index);
        break; }
    default:
        assert(false);
        return InvalidData;
    }
    return m_state;
}

ArgumentList::CursorState ArgumentList::WriteCursor::doWriteString(int lengthPrefixSize)
{
    // TODO request more data when we'd overflow the output buffer

    bool isValidString = false;
    if (m_state == String) {
        isValidString = true; // TODO
    } else if (m_state == ObjectPath) {
        isValidString = true; // TODO
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(array(m_String.begin, m_String.length));
    }
    if (!isValidString) {
        return InvalidData;
    }

    if (lengthPrefixSize == 1) {
        m_data.begin[m_dataPosition] = m_String.length;
    } else {
        basic::writeUint32(m_data.begin + m_dataPosition, m_String.length);
    }
    m_dataPosition += lengthPrefixSize;
    memcpy(m_data.begin + m_dataPosition, m_String.begin, m_String.length);
    m_dataPosition += m_String.length;
    return m_state;
}

void ArgumentList::WriteCursor::advanceState()
{
}

void ArgumentList::WriteCursor::advanceStateFrom(CursorState expectedState)
{
    if (m_state == AnyData || m_state == expectedState) {
        advanceState();
    } else {
        m_state = InvalidData;
    }
}

void ArgumentList::WriteCursor::beginArray(bool isEmpty)
{
}

void ArgumentList::WriteCursor::nextArrayEntry()
{
}

void ArgumentList::WriteCursor::endArray()
{
}

void ArgumentList::WriteCursor::beginDict(bool isEmpty)
{
}

void ArgumentList::WriteCursor::nextDictEntry()
{
}

void ArgumentList::WriteCursor::endDict()
{
}

void ArgumentList::WriteCursor::beginStruct()
{
}

void ArgumentList::WriteCursor::endStruct()
{
}

void ArgumentList::WriteCursor::beginVariant()
{
}

void ArgumentList::WriteCursor::endVariant()
{
}

void ArgumentList::WriteCursor::finish()
{
}

std::vector<ArgumentList::CursorState> ArgumentList::WriteCursor::aggregateStack() const
{
}

void ArgumentList::WriteCursor::writeByte(byte b)
{
}

void ArgumentList::WriteCursor::writeBoolean(bool b)
{
}

void ArgumentList::WriteCursor::writeInt16(int16 i)
{
}

void ArgumentList::WriteCursor::writeUint16(uint16 i)
{
}

void ArgumentList::WriteCursor::writeInt32(int32 i)
{
}

void ArgumentList::WriteCursor::writeUint32(uint32 i)
{
}

void ArgumentList::WriteCursor::writeInt64(int64 i)
{
}

void ArgumentList::WriteCursor::writeUint64(uint64 i)
{
}

void ArgumentList::WriteCursor::writeDouble(double d)
{
}

void ArgumentList::WriteCursor::writeString(array a)
{
}

void ArgumentList::WriteCursor::writeObjectPath(array a)
{
}

void ArgumentList::WriteCursor::writeSignature(array a)
{
}

void ArgumentList::WriteCursor::writeUnixFd(uint32 fd)
{
}
