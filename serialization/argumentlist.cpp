#include "argumentlist.h"

#include "basictypeio.h"

#include <cassert>

static int align(int index, index alignment)
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
        a->chopFirst();
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
        a->chopFirst();
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }
        a->chopFirst();
        nest->endVariant();
        return true;
    case '(':
        if (!nest->beginParen()) {
            return false;
        }
        a->chopFirst();
        while (parseSingleCompleteType(a, nest)) {}
        if (!a->length || *a->begin != ')') {
            return false;
        }
        a->chopFirst();
        nest->endParen();
        return true;
    case 'a':
        if (!nest->beginArray()) {
            return false;
        }
        a->chopFirst();
        if (*a->begin == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || a->length < 4) {
                return false;
            }
            a->chopFirst();
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
            a->chopFirst();
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
bool ArgumentList::isSignatureValid(array signature)
{
    Nesting nest;
    while (signature.length) {
        if (!parseSingleCompleteType(&signature, &nest)) {
            return false;
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
     m_dataPosition(0)
{
    if (m_argList) {
        if (m_data.length > 255) {
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

void ArgumentList::ReadCursor::advanceState()
{
    // general note: if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData

    if (m_state == InvalidData) { // nonrecoverable...
        return;
    }

    assert(m_signature.length - m_signaturePosition >= 0);
    if (m_signature.length - m_signaturePosition < 1) {
        m_state = Finished;
        return;
    }

    const int savedSignaturePosition = m_signaturePosition;
    // in case of aggregate types, it's actually just the alignment
    int requiredDataSize = 4;
    bool isPrimitiveType = false;
    bool isStringType = false;

    // see what the next type is supposed to be
    switch (m_signature.begin[++m_signaturePosition]) {
    case 'y':
        m_state = Byte;
        isPrimitiveType = true;
        requiredDataSize = 1;
        break;
    case 'b':
        m_state = Boolean;
        isPrimitiveType = true;
        break;
    case 'n':
        m_state = Int16;
        isPrimitiveType = true;
        requiredDataSize = 2;
        break;
    case 'q':
        m_state = Uint16;
        isPrimitiveType = true;
        requiredDataSize = 2;
        break;
    case 'i':
        m_state = Int32;
        isPrimitiveType = true;
        break;
    case 'u':
        m_state = Uint32;
        isPrimitiveType = true;
        break;
    case 'x':
        m_state = Int64;
        isPrimitiveType = true;
        requiredDataSize = 8;
        break;
    case 't':
        m_state = Uint64;
        isPrimitiveType = true;
        requiredDataSize = 8;
        break;
    case 'd':
        m_state = Double;
        isPrimitiveType = true;
        requiredDataSize = 8;
        break;
    case 's':
        m_state = String;
        isStringType = true;
        break;
    case 'o':
        m_state = ObjectPath;
        isStringType = true;
        break;
    case 'g':
        m_state = Signature;
        isStringType = true;
        requiredDataSize = 1;
        break;
    case 'h':
        m_state = UnixFd;
        isPrimitiveType = true; // for our purposes it *is* true
        break;
    case 'v':
        if (!m_nesting->beginVariant()) {
            m_state = InvalidData;
            break;
        }
        requiredDataSize = 1;
        break;
    case '(':
        if (!m_nesting->beginParen()) {
            m_state = InvalidData;
            break;
        }
        m_state = BeginStruct;
        requiredDataSize = 8;
        break;
    case ')':
        // TODO
        break;
    case 'a':
        // if the next char is a '{' which starts a dictionary we'll handle it below, so there is
        // no case label for it here
        if (!m_nesting->beginArray()) {
            m_state = InvalidData;
            break;
        }
        m_state = BeginArray;
    case '}':
        // TODO
        m_state = EndDict;
        break;
    default:
        m_state = InvalidData;
        break;
    }

    if (m_state == InvalidData) {
        m_signaturePosition = savedSignaturePosition;
        return;
    }

    // check if we have enough data for the next type, and read it

    const int savedDataPosition = m_dataPosition;
    m_dataPosition = align(m_dataPosition, requiredDataSize);

    // easy cases...
    if (isPrimitiveType) {
        if (m_dataPosition + requiredDataSize > m_data.length) {
            m_state = NeedMoreData;
            m_signaturePosition = savedSignaturePosition;
            m_dataPosition = savedDataPosition;
            return;
        }
        switch(m_state) {
        case Byte:
            m_Byte = m_data.begin[m_dataPosition];
            break;
        case Boolean: {
            uint32 num = basic::readUint32(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            if (num > 1) {
                m_state = InvalidData;
            }
            m_Boolean = num == 1;
            break; }
        case Int16:
            m_Int16 = basic::readInt16(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case Uint16:
            m_Uint16 = basic::readUint16(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case Int32:
            m_Int32 = basic::readInt32(m_data.begin + m_dataPosition, m_args->m_isByteSwapped)
            break;
        case Uint32:
            m_Uint32 = basic::readUint32(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case Int64:
            m_Int64 = basic::readInt64(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case Uint64:
            m_Uint64 = basic::readUint64(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case Double:
            m_Double = basic::readDouble(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            break;
        case UnixFd: {
            uint32 index = basic::readUint32(m_data.begin + m_dataPosition, m_args->m_isByteSwapped);
            uint32 ret; // TODO use index to retrieve the actual file descriptor
            m_UnixFd = ret;
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
        int stringLength = 1; // terminating nul
        if (m_state == Signature) {
            stringLength += m_data.begin[m_dataPosition];
        } else {
            stringLength += basic::readUint32(m_data.begin + m_dataPosition,
                                              m_argList->m_isByteSwapped);
        }
        m_dataPosition += requiredDataSize;
        if (m_dataPosition + stringLength > m_data.length) {
            m_state = NeedMoreData;
            m_signaturePosition = savedSignaturePosition;
            m_dataPosition = savedDataPosition;
            return;
        }
        array ret(m_data.begin + m_dataPosition, stringLength);
        m_String = ret; // TODO validate signature(?), string (no nuls) or object path (cf. spec)
        m_dataPosition += stringLength;
        return;
    }

    // now the interesting part: aggregates
    switch (m_state) {
    case Variant:
    case Struct:
        a->chopFirst();
        while (parseSingleCompleteType(a, m_nesting)) {}
        if (!a->length || *a->begin != ')') {
            return false;
        }
        a->chopFirst();
        m_nesting->endParen();
        return true;

    case Array:
        a->chopFirst();
        if (*a->begin == '{') { // an "array of dict entries", i.e. a dict
            if (!m_nesting->beginParen() || a->length < 4) {
                m_state = InvalidData;
                break;
            }
            a->chopFirst();
            // key must be a basic type
            if (!parseBasicType(a)) {
                return false;
            }
            // value can be any type
            if (!parseSingleCompleteType(a, m_nesting)) {
                return false;
            }
            if (!a->length || *a->begin != '}') {
                return false;
            }
            a->chopFirst();
            m_nesting->endParen();
        } else { // regular array
            if (!parseSingleCompleteType(a, m_nesting)) {
                return false;
            }
        }
        m_nesting->endArray();
        return true;
    default:
        assert(false);
        break;
    }
}

void ArgumentList::ReadCursor::beginArray(int *size);
{
}

void ArgumentList::ReadCursor::endArray();
{
}

bool ArgumentList::ReadCursor::beginDict(int *size);
{
}

bool ArgumentList::ReadCursor::endDict();
{
}

bool ArgumentList::ReadCursor::beginStruct();
{
}

bool ArgumentList::ReadCursor::endStruct();
{
}

bool ArgumentList::ReadCursor::beginVariant();
{
}

bool ArgumentList::ReadCursor::endVariant();
{
}

std::vector<Type> ArgumentList::ReadCursor::aggregateStack() const
{
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
