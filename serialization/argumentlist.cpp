/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "argumentlist.h"

#include "basictypeio.h"
#include "stringtools.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>

// Macros are really ugly, but here every use saves three lines, and it's nice to be able to write
// "data is good if X" instead of "data is bad if Y". That stuff should end up the same after
// optimization anyway.
// while to avoid the dangling-else problem
#define VALID_IF(cond) while (unlikely(!(cond))) { m_state = InvalidData; return; }

// helper to verify the max nesting requirements of the d-bus spec
struct Nesting
{
    Nesting() : array(0), paren(0), variant(0) {}
    static const int arrayMax = 32;
    static const int parenMax = 32;
    static const int totalMax = 64;

    bool beginArray() { array++; return likely(array <= arrayMax && total() <= totalMax); }
    void endArray() { array--; assert(array >= 0); }
    bool beginParen() { paren++; return likely(paren <= parenMax && total() <= totalMax); }
    void endParen() { paren--; assert(paren >= 0); }
    bool beginVariant() { variant++; return likely(total() <= totalMax); }
    void endVariant() { variant--; assert(variant >= 0); }
    int total() { return array + paren + variant; }

    int array;
    int paren;
    int variant;
};

struct NestingWithParenCounter : public Nesting
{
    NestingWithParenCounter() : parenCount(0) {}
    // no need to be virtual, the type will be known statically
    // theoretically it's unnecessary to check the return value: when it is false, the ArgumentList is
    // already invalid so we could abandon all correctness.
    bool beginParen() { bool p = Nesting::beginParen(); parenCount += likely(p) ? 1 : 0; return p; }
    int parenCount;
};

static cstring printableState(ArgumentList::CursorState state)
{
    if (state < ArgumentList::NotStarted || state > ArgumentList::UnixFd) {
        return cstring();
    }
    static const char *strings[ArgumentList::UnixFd + 1] = {
        "NotStarted",
        "Finished",
        "NeedMoreData",
        "InvalidData",
        "AnyData",
        "DictKey",
        "BeginArray",
        "NextArrayEntry",
        "EndArray",
        "BeginDict",
        "NextDictEntry",
        "EndDict",
        "BeginStruct",
        "EndStruct",
        "BeginVariant",
        "EndVariant",
        "Byte",
        "Boolean",
        "Int16",
        "Uint16",
        "Int32",
        "Uint32",
        "Int64",
        "Uint64",
        "Double",
        "String",
        "ObjectPath",
        "Signature",
        "UnixFd"
    };
    return cstring(strings[state]);
}

const int ArgumentList::maxSignatureLength; // for the linker; technically this is required
static const int structAlignment = 8;

ArgumentList::ArgumentList()
   : m_isByteSwapped(false),
     m_readCursorCount(0),
     m_hasWriteCursor(false)
{
}

ArgumentList::ArgumentList(cstring signature, array data, bool isByteSwapped)
   : m_isByteSwapped(isByteSwapped),
     m_readCursorCount(0),
     m_hasWriteCursor(false),
     m_signature(signature),
     m_data(data)
{
}

template<typename T>
std::string printMaybeNil(bool isNil, T value, const char *typeName)
{
    std::stringstream ret;
    ret << typeName << ": ";
    if (isNil) {
        ret << "<nil>\n";
    } else {
        ret << value << '\n';
    }
    return ret.str();
}

template<>
std::string printMaybeNil<cstring>(bool isNil, cstring cstr, const char *typeName)
{
    std::stringstream ret;
    ret << typeName << ": ";
    if (isNil) {
        ret << "<nil>\n";
    } else {
        ret << '"' << toStdString(cstr) << "\"\n";
    }
    return ret.str();
}

std::string ArgumentList::prettyPrint() const
{
    ReadCursor reader = const_cast<ArgumentList*>(this)->beginRead();
    if (!reader.isValid()) {
        return std::string();
    }
    std::stringstream ret;
    std::string nestingPrefix;

    bool isDone = false;
    int emptyNesting = 0;

    while (!isDone) {
        switch(reader.state()) {
        case ArgumentList::Finished:
            assert(nestingPrefix.empty());
            isDone = true;
            break;
        case ArgumentList::BeginStruct:
            reader.beginStruct();
            ret << nestingPrefix << "begin struct\n";
            nestingPrefix += "( ";
            break;
        case ArgumentList::EndStruct:
            reader.endStruct();
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end struct\n";
            break;
        case ArgumentList::BeginVariant:
            reader.beginVariant();
            ret << nestingPrefix << "begin variant\n";
            nestingPrefix += "v ";
            break;
        case ArgumentList::EndVariant:
            reader.endVariant();
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end variant\n";
            break;
        case ArgumentList::BeginArray: {
            bool isEmpty;
            reader.beginArray(&isEmpty);
            emptyNesting += isEmpty ? 1 : 0;
            ret << nestingPrefix << "begin array\n";
            nestingPrefix += "[ ";
            break; }
        case ArgumentList::NextArrayEntry:
            reader.nextArrayEntry();
            break;
        case ArgumentList::EndArray:
            reader.endArray();
            emptyNesting = std::max(emptyNesting - 1, 0);
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end array\n";
            break;
        case ArgumentList::BeginDict: {
            bool isEmpty = false;
            reader.beginDict(&isEmpty);
            emptyNesting += isEmpty ? 1 : 0;
            ret << nestingPrefix << "begin dict\n";
            nestingPrefix += "{ ";
            break; }
        case ArgumentList::NextDictEntry:
            reader.nextDictEntry();
            break;
        case ArgumentList::EndDict:
            reader.endDict();
            emptyNesting = std::max(emptyNesting - 1, 0);
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end dict\n";
            break;
        case ArgumentList::Byte:
            ret << nestingPrefix << printMaybeNil(emptyNesting, int(reader.readByte()), "byte");
            break;
        case ArgumentList::Boolean: {
            bool b = reader.readBoolean();
            ret << nestingPrefix << "bool: ";
            if (emptyNesting) {
                ret << "<nil>";
            } else {
                ret << (b ? "true" : "false");
            }
            ret << '\n';
            break; }
        case ArgumentList::Int16:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readInt16(), "int16");
            break;
        case ArgumentList::Uint16:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readUint16(), "uint16");
            break;
        case ArgumentList::Int32:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readInt32(), "int32");
            break;
        case ArgumentList::Uint32:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readUint32(), "uint32");
            break;
        case ArgumentList::Int64:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readInt64(), "int64");
            break;
        case ArgumentList::Uint64:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readUint64(), "uint64");
            break;
        case ArgumentList::Double:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readDouble(), "double");
            break;
        case ArgumentList::String:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readString(), "string");
            break;
        case ArgumentList::ObjectPath:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readObjectPath(), "object path");
            break;
        case ArgumentList::Signature:
            ret << nestingPrefix << printMaybeNil(emptyNesting, reader.readSignature(), "type signature");
            break;
        case ArgumentList::UnixFd:
            // TODO
            break;
        case ArgumentList::InvalidData:
        case ArgumentList::NeedMoreData:
        default: {
            return std::string("<error: ") +
                   toStdString(reader.stateString()) + ">\n";
            break; }
        }
    }
    return ret.str();
}

ArgumentList::ReadCursor ArgumentList::beginRead()
{
    ArgumentList *thisInstance = 0;
    if (!m_hasWriteCursor) {
        m_readCursorCount++;
        thisInstance = this;
    }
    return ReadCursor(thisInstance);
}

ArgumentList::WriteCursor ArgumentList::beginWrite()
{
    ArgumentList *thisInstance = 0;
    if (!m_readCursorCount && !m_hasWriteCursor) {
        m_hasWriteCursor = true;
        thisInstance = this;
    }
    return WriteCursor(thisInstance);
}

static void chopFirst(cstring *s)
{
    s->begin++;
    s->length--;
}

// static
bool ArgumentList::isStringValid(cstring string)
{
    if (!string.begin || string.begin[string.length] != 0) {
        return false;
    }
    for (int i = 0; i < string.length; i++) {
        if (string.begin[i] == 0) {
            return false;
        }
    }
    return true;
}

static inline bool isObjectNameLetter(byte b)
{
    return likely((b >= 'a' && b <= 'z') || b == '_' || (b >= 'A' && b <= 'Z') || (b >= '0' && b <= '9'));
}

// static
bool ArgumentList::isObjectPathValid(cstring path)
{
    if (!path.begin || path.begin[path.length] != 0) {
        return false;
    }
    byte prevLetter = path.begin[0];
    if (prevLetter != '/') {
        return false;
    }
    if (path.length == 1) {
        return true; // "/" special case
    }
    for (int i = 1; i < path.length; i++) {
        byte currentLetter = path.begin[i];
        if (prevLetter == '/') {
            if (!isObjectNameLetter(currentLetter)) {
                return false;
            }
        } else {
            if (currentLetter != '/' && !isObjectNameLetter(currentLetter)) {
                return false;
            }
        }
        prevLetter = currentLetter;
    }
    return prevLetter != '/';
}

// static
bool ArgumentList::isObjectPathElementValid(cstring pathElement)
{
    if (!pathElement.length) {
        return false;
    }
    for (int i = 0; i < pathElement.length; i++) {
        if (!isObjectNameLetter(pathElement.begin[i])) {
            return false;
        }
    }
    return true;
}

static bool parseBasicType(cstring *s)
{
    // ### not checking if zero-terminated
    assert(s->begin);
    if (s->length < 0) {
        return false;
    }
    switch (*s->begin) {
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
        chopFirst(s);
        return true;
    default:
        return false;
    }
}

static bool parseSingleCompleteType(cstring *s, Nesting *nest)
{
    assert(s->begin);
    // ### not cheching if zero-terminated

    switch (*s->begin) {
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
        chopFirst(s);
        return true;
    case 'v':
        if (!nest->beginVariant()) {
            return false;
        }
        chopFirst(s);
        nest->endVariant();
        return true;
    case '(': {
        if (!nest->beginParen()) {
            return false;
        }
        chopFirst(s);
        bool isEmptyStruct = true;
        while (parseSingleCompleteType(s, nest)) {
            isEmptyStruct = false;
        }
        if (!s->length || *s->begin != ')' || isEmptyStruct) {
            return false;
        }
        chopFirst(s);
        nest->endParen();
        return true; }
    case 'a':
        if (!nest->beginArray()) {
            return false;
        }
        chopFirst(s);
        if (*s->begin == '{') { // an "array of dict entries", i.e. a dict
            if (!nest->beginParen() || s->length < 4) {
                return false;
            }
            chopFirst(s);
            // key must be a basic type
            if (!parseBasicType(s)) {
                return false;
            }
            // value can be any type
            if (!parseSingleCompleteType(s, nest)) {
                return false;
            }
            if (!s->length || *s->begin != '}') {
                return false;
            }
            chopFirst(s);
            nest->endParen();
        } else { // regular array
            if (!parseSingleCompleteType(s, nest)) {
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
bool ArgumentList::isSignatureValid(cstring signature, SignatureType type)
{
    Nesting nest;
    if (!signature.begin || signature.begin[signature.length] != 0) {
        return false;
    }
    if (type == VariantSignature) {
        if (!signature.length) {
            return false;
        }
        if (!parseSingleCompleteType(&signature, &nest)) {
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
     m_signaturePosition(-1),
     m_dataPosition(0),
     m_zeroLengthArrayNesting(0)
{
    VALID_IF(m_argList);
    m_signature = m_argList->m_signature;
    m_data = m_argList->m_data;
    VALID_IF(ArgumentList::isSignatureValid(m_signature));
    advanceState();
}

ArgumentList::ReadCursor::~ReadCursor()
{
    if (m_argList) {
        m_argList->m_readCursorCount -= 1;
    }
    delete m_nesting;
    m_nesting = 0;
}

cstring ArgumentList::ReadCursor::stateString() const
{
    return printableState(m_state);
}

void ArgumentList::ReadCursor::replaceData(array data)
{
    VALID_IF(data.length >= m_dataPosition);

    ptrdiff_t offset = data.begin - m_data.begin;

    // fix up saved signatures on the aggregate stack except for the first, which is not contained in m_data
    bool isOriginalSignature = true;
    const int size = m_aggregateStack.size();
    for (int i = 0; i < size; i++) {
        if (m_aggregateStack[i].aggregateType == BeginVariant) {
            if (isOriginalSignature) {
                isOriginalSignature = false;
            } else {
                m_aggregateStack[i].var.prevSignature.begin += offset;
            }
        }
    }
    if (!isOriginalSignature) {
        m_signature.begin += offset;
    }

    m_data = data;
    if (m_state == NeedMoreData) {
        advanceState();
    }
}

static void getTypeInfo(byte letterCode, ArgumentList::CursorState *typeState, uint32 *alignment,
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
        break;
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
    if (typeState) {
        *typeState = state;
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
    uint32 stringLength = 1;
    if (lengthPrefixSize == 1) {
        stringLength += m_data.begin[m_dataPosition];
    } else {
        stringLength += basic::readUint32(m_data.begin + m_dataPosition,
                                          m_argList->m_isByteSwapped);
    }
    m_dataPosition += lengthPrefixSize;
    if (unlikely(m_dataPosition + stringLength > m_data.length)) {
        return NeedMoreData;
    }
    m_String.begin = m_data.begin + m_dataPosition;
    m_String.length = stringLength - 1; // terminating null is not counted
    m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_String.begin, m_String.length));
    }
    if (unlikely(!isValidString)) {
        return InvalidData;
    }
    return m_state;
}

void ArgumentList::ReadCursor::advanceState()
{
    // if we don't have enough data, the strategy is to keep everything unchanged
    // except for the state which will be NeedMoreData
    // we don't have to deal with invalid signatures here because they are checked beforehand EXCEPT
    // for aggregate nesting which cannot be checked using only one signature, due to variants.
    // variant signatures are only parsed while reading the data. individual variant signatures
    // ARE checked beforehand whenever we find one in this method.

    if (unlikely(m_state == InvalidData)) { // nonrecoverable...
        return;
    }

    assert(m_signaturePosition < m_signature.length);

    const int savedSignaturePosition = m_signaturePosition;
    const int savedDataPosition = m_dataPosition;

    m_signaturePosition++;

    // check if we are about to close any aggregate or even the whole argument list
    if (m_aggregateStack.empty()) {
        if (m_signaturePosition >= m_signature.length) {
            m_state = Finished;
            return;
        }
    } else {
        const AggregateInfo &aggregateInfo = m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginStruct:
            break; // handled later by getTypeInfo recognizing ')' -> EndStruct
        case BeginVariant:
            if (m_signaturePosition >= m_signature.length) {
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
            if (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 2) {
                m_state = NextDictEntry;
                return;
            }
            break;
        case BeginArray:
            if (m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                m_state = NextArrayEntry;
                return;
            }
            break;
        default:
            break;
        }
    }

    // for aggregate types, it's just the alignment. for primitive types, it's also the actual size.
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    getTypeInfo(m_signature.begin[m_signaturePosition],
                &m_state, &alignment, &isPrimitiveType, &isStringType);

    if (unlikely(m_state == InvalidData)) {
        return;
    }

    // check if we have enough data for the next type, and read it
    // if we're in a zero-length array, we are iterating only over the types without reading any data

    if (likely(!m_zeroLengthArrayNesting)) {
        int padStart = m_dataPosition;
        m_dataPosition = align(m_dataPosition, alignment);
        if (unlikely(m_dataPosition > m_data.length)) {
            goto out_needMoreData;
        }
        VALID_IF(isPaddingZero(m_data, padStart, m_dataPosition));

        if (isPrimitiveType || isStringType) {
            if (unlikely(m_dataPosition + alignment > m_data.length)) {
                goto out_needMoreData;
            }

            if (isPrimitiveType) {
                m_state = doReadPrimitiveType();
                m_dataPosition += alignment;
            } else {
                m_state = doReadString(alignment);
                if (unlikely(m_state == NeedMoreData)) {
                    goto out_needMoreData;
                }
            }
            return;
        }
    } else {
        if (isPrimitiveType || isStringType) {
            return; // nothing to do! (readFoo() will return "random" data, so don't use that data!)
        }
    }

    // now the interesting part: aggregates

    AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        VALID_IF(m_nesting->beginParen());
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
        cstring signature;
        if (unlikely(m_zeroLengthArrayNesting)) {
            static const char *emptyString = "";
            signature = cstring(emptyString, 0);
        } else {
            if (unlikely(m_dataPosition >= m_data.length)) {
                goto out_needMoreData;
            }
            signature.length = m_data.begin[m_dataPosition++];
            signature.begin = m_data.begin + m_dataPosition;
            m_dataPosition += signature.length + 1;
            if (unlikely(m_dataPosition > m_data.length)) {
                goto out_needMoreData;
            }
            VALID_IF(ArgumentList::isSignatureValid(signature, ArgumentList::VariantSignature));
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        VALID_IF(m_nesting->beginVariant());

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
        if (likely(!m_zeroLengthArrayNesting)) {
            if (unlikely(m_dataPosition + 4 > m_data.length)) {
                goto out_needMoreData;
            }
            static const int maxArrayDataLength = 67108864; // from the spec
            arrayLength = basic::readUint32(m_data.begin + m_dataPosition, m_argList->m_isByteSwapped);
            VALID_IF(arrayLength <= maxArrayDataLength);
            m_dataPosition += 4;
        }

        CursorState firstElementType;
        uint32 firstElementAlignment;
        getTypeInfo(m_signature.begin[m_signaturePosition + 1],
                    &firstElementType, &firstElementAlignment, 0, 0);

        m_state = firstElementType == BeginDict ? BeginDict : BeginArray;
        aggregateInfo.aggregateType = m_state;

        // ### are we supposed to align m_dataPosition if the array is empty?
        if (likely(!m_zeroLengthArrayNesting)) {
            int padStart = m_dataPosition;
            m_dataPosition = align(m_dataPosition, firstElementAlignment);
            VALID_IF(isPaddingZero(m_data, padStart, m_dataPosition));
            aggregateInfo.arr.dataEnd = m_dataPosition + arrayLength;
            if (unlikely(aggregateInfo.arr.dataEnd > m_data.length)) {
                // NB: do not clobber (the unsaved) nesting before potentially going to out_needMoreData!
                goto out_needMoreData;
            }
        }
        VALID_IF(m_nesting->beginArray());
        if (firstElementType == BeginDict) {
            m_signaturePosition++;
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(m_nesting->beginParen());
        }

        // position at the 'a' or '{' because we increment m_signaturePosition before reading a char
        aggregateInfo.arr.containedTypeBegin = m_signaturePosition;
        if (!arrayLength) {
            m_zeroLengthArrayNesting++;
        }

        m_aggregateStack.push_back(aggregateInfo);
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    // we only start an array when the data for it has fully arrived (possible due to the length
    // prefix), so if we still run out of data in an array the input is inconsistent.
    VALID_IF(!m_nesting->array);
    m_state = NeedMoreData;
    m_signaturePosition = savedSignaturePosition;
    m_dataPosition = savedDataPosition;
}

void ArgumentList::ReadCursor::advanceStateFrom(CursorState expectedState)
{
    // Calling this method could be replaced with using VALID_IF in the callers, but it currently
    // seems more conventient like this.
    VALID_IF(m_state == expectedState);
    advanceState();
}

void ArgumentList::ReadCursor::beginArrayOrDict(bool isDict, bool *isEmpty)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (isEmpty) {
        *isEmpty = m_zeroLengthArrayNesting;
    }

    if (unlikely(m_zeroLengthArrayNesting)) {
        if (!isEmpty) {
            // need to move m_signaturePosition to the end of the array signature *here* or it won't happen

            // fix up nesting and parse position before parsing the array signature
            if (isDict) {
                m_nesting->endParen();
                m_signaturePosition--; // it was moved ahead by one to skip the '{'
            }
            m_nesting->endArray();

            // parse the array signature in order to skip it
            // barring bugs, must have been too deep nesting inside variants if parsing fails
            cstring sigTail(m_signature.begin + m_signaturePosition, m_signature.length - m_signaturePosition);
            VALID_IF(parseSingleCompleteType(&sigTail, m_nesting));
            m_signaturePosition = m_signature.length - sigTail.length - 1;

            // un-fix up nesting
            m_nesting->beginArray();
            if (isDict) {
                m_nesting->beginParen();
            }
        }
    }
    m_state = isDict ? NextDictEntry : NextArrayEntry;
}

// TODO introduce an error state different from InvalidData when the wrong method is called
void ArgumentList::ReadCursor::beginArray(bool *isEmpty)
{
    VALID_IF(m_state == BeginArray);
    beginArrayOrDict(false, isEmpty);
}

bool ArgumentList::ReadCursor::nextArrayOrDictEntry(bool isDict)
{
    assert(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (unlikely(m_zeroLengthArrayNesting)) {
        if (m_signaturePosition <= aggregateInfo.arr.containedTypeBegin) {
            // do one iteration to read the types; read the next type...
            advanceState();
            // theoretically, nothing can go wrong: the signature is pre-validated and we are not going
            // to read any data. also theoretically, there are no bugs in advanceState() :)
            return m_state != InvalidData;
        } else {
            // second iteration or skipping an empty array
            m_zeroLengthArrayNesting--;
        }
    } else {
        if (m_dataPosition < aggregateInfo.arr.dataEnd) {
            if (isDict) {
                m_dataPosition = align(m_dataPosition, 8); // align to dict entry
            }
            // rewind to start of contained type and read the type info there
            m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
            advanceState();
            return m_state != InvalidData;
        }
    }
    // no more iterations
    m_state = isDict ? EndDict : EndArray;
    m_signaturePosition--; // this was increased in advanceState() before sending us here
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

void ArgumentList::ReadCursor::beginDict(bool *isEmpty)
{
    VALID_IF(m_state == BeginDict);
    beginArrayOrDict(true, isEmpty);
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

void ArgumentList::ReadCursor::endDict()
{
    advanceStateFrom(EndDict);
}

void ArgumentList::ReadCursor::beginStruct()
{
    advanceStateFrom(BeginStruct);
}

void ArgumentList::ReadCursor::endStruct()
{
    advanceStateFrom(EndStruct);
}

void ArgumentList::ReadCursor::beginVariant()
{
    advanceStateFrom(BeginVariant);
}

void ArgumentList::ReadCursor::endVariant()
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
     m_state(AnyData),
     m_nesting(new NestingWithParenCounter),
     m_signature(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0),
     m_signaturePosition(0),
     m_data(reinterpret_cast<byte *>(malloc(InitialDataCapacity))),
     m_dataCapacity(InitialDataCapacity),
     m_dataPosition(0),
     m_zeroLengthArrayNesting(0)
{
}

ArgumentList::WriteCursor::~WriteCursor()
{
    if (m_argList) {
        assert(m_argList->m_hasWriteCursor);
        m_argList->m_hasWriteCursor = false;
    }
    free(m_data);
    m_data = 0;
    delete m_nesting;
    m_nesting = 0;
}

cstring ArgumentList::WriteCursor::stateString() const
{
    return printableState(m_state);
}

ArgumentList::CursorState ArgumentList::WriteCursor::doWritePrimitiveType(uint32 alignAndSize)
{
    m_dataPosition = align(m_dataPosition, alignAndSize);
    const uint32 newDataPosition = m_dataPosition + alignAndSize;
    if (unlikely(newDataPosition > m_dataCapacity)) {
        m_dataCapacity *= 2;
        m_data = reinterpret_cast<byte *>(realloc(m_data, m_dataCapacity));
    }

    switch(m_state) {
    case Byte:
        m_data[m_dataPosition] = m_Byte;
        break;
    case Boolean: {
        uint32 num = m_Boolean ? 1 : 0;
        basic::writeUint32(m_data + m_dataPosition, num);
        break; }
    case Int16:
        basic::writeInt16(m_data + m_dataPosition, m_Int16);
        break;
    case Uint16:
        basic::writeUint16(m_data + m_dataPosition, m_Uint16);
        break;
    case Int32:
        basic::writeInt32(m_data + m_dataPosition, m_Int32);
        break;
    case Uint32:
        basic::writeUint32(m_data + m_dataPosition, m_Uint32);
        break;
    case Int64:
        basic::writeInt64(m_data + m_dataPosition, m_Int64);
        break;
    case Uint64:
        basic::writeUint64(m_data + m_dataPosition, m_Uint64);
        break;
    case Double:
        basic::writeDouble(m_data + m_dataPosition, m_Double);
        break;
    case UnixFd: {
        uint32 index; // TODO = index of the FD we actually want to send
        basic::writeUint32(m_data + m_dataPosition, index);
        break; }
    default:
        assert(false);
        return InvalidData;
    }

    m_dataPosition = newDataPosition;
    m_elements.push_back(ElementInfo(alignAndSize, alignAndSize));
    return m_state;
}

ArgumentList::CursorState ArgumentList::WriteCursor::doWriteString(int lengthPrefixSize)
{
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_String.begin, m_String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_String.begin, m_String.length));
    }
    if (unlikely(!isValidString)) {
        return InvalidData;
    }

    m_dataPosition = align(m_dataPosition, lengthPrefixSize);
    const uint32 newDataPosition = m_dataPosition + lengthPrefixSize + m_String.length + 1;
    if (unlikely(newDataPosition > m_dataCapacity)) {
        while (newDataPosition > m_dataCapacity) {
            m_dataCapacity *= 2;
        }
        m_data = reinterpret_cast<byte *>(realloc(m_data, m_dataCapacity));
    }

    if (lengthPrefixSize == 1) {
        m_data[m_dataPosition] = m_String.length;
    } else {
        basic::writeUint32(m_data + m_dataPosition, m_String.length);
    }
    m_dataPosition += lengthPrefixSize;
    m_elements.push_back(ElementInfo(lengthPrefixSize, lengthPrefixSize));

    memcpy(m_data + m_dataPosition, m_String.begin, m_String.length + 1);
    m_dataPosition = newDataPosition;
    for (uint32 l = m_String.length + 1; l; ) {
        uint32 chunkSize = std::min(l, uint32(ElementInfo::LargestSize));
        m_elements.push_back(ElementInfo(1, chunkSize));
        l -= chunkSize;
    }

    return m_state;
}

void ArgumentList::WriteCursor::advanceState(array signatureFragment, CursorState newState)
{
    // what needs to happen here:
    // - if we are in an existing portion of the signature (like writing the >1st iteration of an array)
    //   check if the type to be written is the same as the one that's already in the signature
    //   - otherwise we still need to check if the data we're adding conforms with the spec, e.g.
    //     no empty structs, dict entries must have primitive key type and exactly one value type
    // - check well-formedness of data: strings, maximum serialized array length and message length
    //   (variant signature length only being known after finishing a variant introduces uncertainty
    //    of final data stream size - due to alignment padding, a variant signature longer by one can
    //    cause an up to seven bytes longer message. in other cases it won't change message length at all.)
    // - increase size of data buffer when it gets too small
    // - store information about variants and arrays, in order to:
    //   - know what the final binary message size will be
    //   - in finish(), create the final data stream with inline variant signatures and array lengths

    if (unlikely(m_state == InvalidData)) {
        return;
    }

    m_state = newState;
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    if (signatureFragment.length) {
        getTypeInfo(signatureFragment.begin[0], 0, &alignment, &isPrimitiveType, &isStringType);
    }

    bool isWritingSignature = m_signaturePosition == m_signature.length; // TODO correct?
    if (isWritingSignature) {
        // signature additions must conform to syntax
        VALID_IF(m_signaturePosition + signatureFragment.length <= maxSignatureLength);

        if (!m_aggregateStack.empty()) {
            const AggregateInfo &aggregateInfo = m_aggregateStack.back();
            switch (aggregateInfo.aggregateType) {
            case BeginVariant:
                // arrays and variants may contain just one single complete type (note that this will
                // trigger only when not inside an aggregate inside the variant or array)
                if (m_signaturePosition >= 1) {
                    VALID_IF(m_state == EndVariant);
                }
                break;
            case BeginArray:
                if (m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1) {
                    VALID_IF(m_state == EndArray);
                }
                break;
            case BeginDict:
                if (m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
                    VALID_IF(isPrimitiveType || isStringType);
                }
                // first type has been checked already, second must be present (checked in EndDict
                // state handler). no third type allowed.
                if (m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2) {
                    VALID_IF(m_state == EndDict);
                }
                break;
            default:
                break;
            }
        }

        // finally, extend the signature
        for (int i = 0; i < signatureFragment.length; i++) {
            m_signature.begin[m_signaturePosition++] = signatureFragment.begin[i];
        }
        m_signature.length += signatureFragment.length;
    } else {
        // signature must match first iteration (of an array/dict)
        VALID_IF(m_signaturePosition + signatureFragment.length <= m_signature.length);
        // TODO need to apply special checks for state changes with no explicit signature char?
        // (end of array, end of variant and such)
        for (int i = 0; i < signatureFragment.length; i++) {
            VALID_IF(m_signature.begin[m_signaturePosition++] == signatureFragment.begin[i]);
        }
    }

    if (isPrimitiveType) {
        m_state = doWritePrimitiveType(alignment);
        return;
    }
    if (isStringType) {
        m_state = doWriteString(alignment);
        return;
    }

    AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        VALID_IF(m_nesting->beginParen());
        aggregateInfo.aggregateType = BeginStruct;
        aggregateInfo.sct.containedTypeBegin = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);
        m_elements.push_back(ElementInfo(alignment, 0)); // align only
        break;
    case EndStruct:
        m_nesting->endParen();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginStruct &&
                 m_signaturePosition > aggregateInfo.sct.containedTypeBegin + 1); // no empty structs
        m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        VALID_IF(m_nesting->beginVariant());
        aggregateInfo.aggregateType = BeginVariant;
        aggregateInfo.var.prevSignature.begin = m_signature.begin;
        aggregateInfo.var.prevSignature.length = m_signature.length;
        aggregateInfo.var.prevSignaturePosition = m_signaturePosition;
        aggregateInfo.var.signatureIndex = m_variantSignatures.size();
        m_aggregateStack.push_back(aggregateInfo);

        // arrange for finish() to take a signature from m_variantSignatures
        m_elements.push_back(ElementInfo(1, ElementInfo::VariantSignature));
        cstring str(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0);
        m_variantSignatures.push_back(str);
        m_signature = str;
        m_signaturePosition = 0;
        break; }
    case EndVariant: {
        m_nesting->endVariant();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginVariant);
        if (likely(!m_zeroLengthArrayNesting)) {
            // apparently, empty variants are not allowed. as an exception, in zero length arrays they are
            // suitable for writing a type signature like "av" in the shortest possible way.
            VALID_IF(m_signaturePosition > 0);
        }
        m_signature.begin[m_signaturePosition] = '\0';
        assert(aggregateInfo.var.signatureIndex < m_variantSignatures.size());
        m_variantSignatures[aggregateInfo.var.signatureIndex].length = m_signaturePosition;
        assert(m_variantSignatures[aggregateInfo.var.signatureIndex].begin = m_signature.begin);

        m_signature.begin = aggregateInfo.var.prevSignature.begin;
        m_signature.length = aggregateInfo.var.prevSignature.length;
        m_signaturePosition = aggregateInfo.var.prevSignaturePosition;
        m_aggregateStack.pop_back();
        break; }

    case BeginDict:
    case BeginArray: {
        VALID_IF(m_nesting->beginArray());
        if (m_state == BeginDict) {
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(m_nesting->beginParen());
        }
        aggregateInfo.aggregateType = m_state;
        aggregateInfo.arr.dataBegin = m_dataPosition;
        aggregateInfo.arr.containedTypeBegin = m_signaturePosition;
        m_aggregateStack.push_back(aggregateInfo);

        m_elements.push_back(ElementInfo(4, ElementInfo::ArrayLengthField));
        if (m_state == BeginDict) {
            m_elements.push_back(ElementInfo(structAlignment, 0)); // align to dict entry
            m_state = DictKey;
            return;
        }
        break; }

    case EndDict:
    case EndArray: {
        const bool isDict = m_state == EndDict;
        if (isDict) {
            m_nesting->endParen();
        }
        m_nesting->endArray();
        VALID_IF(!m_aggregateStack.empty());
        aggregateInfo = m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));
        VALID_IF(m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + (isDict ? 3 : 1));
        m_aggregateStack.pop_back();
        if (unlikely(m_zeroLengthArrayNesting)) {
            if (!--m_zeroLengthArrayNesting) {
                // last chance to erase data inside the empty array so it doesn't end up in the output
                m_variantSignatures.erase(m_variantSignatures.begin() +
                                          m_variantSignaturesCountBeforeZeroLengthArray,
                                          m_variantSignatures.end());
                m_elements.erase(m_elements.begin() + m_dataElementsCountBeforeZeroLengthArray,
                                 m_elements.end());
                m_dataPosition = m_dataPositionBeforeZeroLengthArray;
            }
        }

        // ### not checking array size here, it may change by a few bytes in the final data stream
        //     due to alignment changes from a different start address
        m_elements.push_back(ElementInfo(1, ElementInfo::ArrayLengthEndMark));
        break; }
    }

    m_state = AnyData;
}

void ArgumentList::WriteCursor::beginArrayOrDict(bool isDict, bool isEmpty)
{
    isEmpty = isEmpty || m_zeroLengthArrayNesting;
    if (isEmpty) {
        if (!m_zeroLengthArrayNesting++) {
            // for simplictiy and performance in the fast path, we keep storing the data chunks and any
            // variant signatures written inside an empty array. when we close the array, though, we
            // throw away all that data and signatures and keep only changes in the signature containing
            // the topmost empty array.
            m_variantSignaturesCountBeforeZeroLengthArray = m_variantSignatures.size();
            m_dataElementsCountBeforeZeroLengthArray = m_elements.size() + 1; // +1 -> keep ArrayLengthField
            m_dataPositionBeforeZeroLengthArray = m_dataPosition;
        }
    }
    if (isDict) {
        advanceState(array("a{", strlen("a{")), BeginDict);
    } else {
        advanceState(array("a", strlen("a")), BeginArray);
    }
}

void ArgumentList::WriteCursor::beginArray(bool isEmpty)
{
    beginArrayOrDict(false, isEmpty);
}

void ArgumentList::WriteCursor::nextArrayOrDictEntry(bool isDict)
{
    // TODO sanity / syntax checks, data length check too?

    VALID_IF(!m_aggregateStack.empty());
    AggregateInfo &aggregateInfo = m_aggregateStack.back();
    VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (unlikely(m_zeroLengthArrayNesting)) {
        // allow one iteration to write the types
        VALID_IF(m_signaturePosition == aggregateInfo.arr.containedTypeBegin);
    } else {
        if (m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
            // first iteration, nothing to do.
            // this is due to the feature that nextFooEntry() is not required before the first element.
        } else if (isDict) {
            // a dict must have a key and value
            VALID_IF(m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1);

            m_nesting->parenCount += 1; // for alignment accounting
            m_elements.push_back(ElementInfo(structAlignment, 0)); // align to dict entry
        }
        // array case: we are not at start of contained type's signature, the array is at top of stack
        // -> we *are* at the end of a single complete type inside the array, syntax check passed
        m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
    }
}

void ArgumentList::WriteCursor::nextArrayEntry()
{
    nextArrayOrDictEntry(false);
}

void ArgumentList::WriteCursor::endArray()
{
    advanceState(array(), EndArray);
}

void ArgumentList::WriteCursor::beginDict(bool isEmpty)
{
    beginArrayOrDict(true, isEmpty);
}

void ArgumentList::WriteCursor::nextDictEntry()
{
    nextArrayOrDictEntry(true);
}

void ArgumentList::WriteCursor::endDict()
{
    advanceState(array("}", strlen("}")), EndDict);
}

void ArgumentList::WriteCursor::beginStruct()
{
    advanceState(array("(", strlen("(")), BeginStruct);
}

void ArgumentList::WriteCursor::endStruct()
{
    advanceState(array(")", strlen(")")), EndStruct);
}

void ArgumentList::WriteCursor::beginVariant()
{
    advanceState(array("v", strlen("v")), BeginVariant);
}

void ArgumentList::WriteCursor::endVariant()
{
    advanceState(array(), EndVariant);
}

struct ArrayLengthField
{
    uint32 lengthFieldPosition;
    uint32 dataStartPosition;
};

void ArgumentList::WriteCursor::finish()
{
    // what needs to happen here:
    // - check if the message can be closed - basically the aggregate stack must be empty
    // - assemble the message, inserting variant signatures and array lengths
    // - close the signature by adding the terminating null
    if (m_state == InvalidData) {
        return;
    }
    assert(m_nesting->total() == 0);
    assert(!m_zeroLengthArrayNesting);
    assert(m_signaturePosition <= maxSignatureLength); // this should have been caught before
    m_signature.begin[m_signaturePosition] = '\0';
    m_signature.length = m_signaturePosition;

    m_dataPosition = 0;

    // The maximum alignment blowup for naturally aligned types is just less than a factor of 2.
    // Structs and dict entries are always 8 byte aligned so they add a maximum blowup of 7 bytes
    // each (when they contain a byte).
    // Those estimates are very conservative (but easy!), so some space optimization is possible.
    const int bufferSize = m_dataCapacity * 2 + m_nesting->parenCount * 7;
    byte *buffer = reinterpret_cast<byte *>(malloc(std::max(int(InitialDataCapacity), bufferSize)));
    int bufferPos = 0;
    uint32 count = m_elements.size();
    int variantSignatureIndex = 0;

    std::vector<ArrayLengthField> lengthFieldStack;

    for (uint32 i = 0; i < count; i++) {
        ElementInfo ei = m_elements[i];
        if (ei.size <= ElementInfo::LargestSize) {
            // copy data chunks while applying the proper alignment
            zeroPad(buffer, ei.alignment(), &bufferPos);
            // if !ei.size, it's alignment padding which does not apply to source data
            if (ei.size) {
                m_dataPosition = align(m_dataPosition, ei.alignment());
                memcpy(buffer + bufferPos, m_data + m_dataPosition, ei.size);
                bufferPos += ei.size;
                m_dataPosition += ei.size;
            }
        } else {
            // the value of ei.size has special meaning
            ArrayLengthField al;
            if (ei.size == ElementInfo::ArrayLengthField) {
                // start of an array
                // reserve space for the array length prefix
                zeroPad(buffer, ei.alignment(), &bufferPos);
                al.lengthFieldPosition = bufferPos;
                bufferPos += 4;
                // array data starts aligned to the first array element
                zeroPad(buffer, m_elements[i + 1].alignment(), &bufferPos);
                al.dataStartPosition = bufferPos;
                lengthFieldStack.push_back(al);
            } else if (ei.size == ElementInfo::ArrayLengthEndMark) {
                // end of an array - just put the now known array length in front of the array
                al = lengthFieldStack.back();
                basic::writeUint32(buffer + al.lengthFieldPosition, bufferPos - al.dataStartPosition);
                lengthFieldStack.pop_back();
            } else { // ei.size == ElementInfo::VariantSignature
                // fill in signature (should already include trailing null)
                cstring signature = m_variantSignatures[variantSignatureIndex++];
                buffer[bufferPos++] = signature.length;
                memcpy(buffer + bufferPos, signature.begin, signature.length + 1);
                bufferPos += signature.length + 1;
                free(signature.begin);
            }
        }
    }
    assert(variantSignatureIndex == m_variantSignatures.size());
    assert(lengthFieldStack.empty());
    m_elements.clear();
    m_variantSignatures.clear();

    m_argList->m_signature = m_signature;
    m_argList->m_data = array(buffer, bufferPos);
}

std::vector<ArgumentList::CursorState> ArgumentList::WriteCursor::aggregateStack() const
{
    const int count = m_aggregateStack.size();
    std::vector<CursorState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(m_aggregateStack[i].aggregateType);
    }
    return ret;
}

void ArgumentList::WriteCursor::writeByte(byte b)
{
    m_Byte = b;
    advanceState(array("y", strlen("y")), Byte);
}

void ArgumentList::WriteCursor::writeBoolean(bool b)
{
    m_Boolean = b;
    advanceState(array("b", strlen("b")), Boolean);
}

void ArgumentList::WriteCursor::writeInt16(int16 i)
{
    m_Int16 = i;
    advanceState(array("n", strlen("n")), Int16);
}

void ArgumentList::WriteCursor::writeUint16(uint16 i)
{
    m_Uint16 = i;
    advanceState(array("q", strlen("q")), Uint16);
}

void ArgumentList::WriteCursor::writeInt32(int32 i)
{
    m_Int32 = i;
    advanceState(array("i", strlen("i")), Int32);
}

void ArgumentList::WriteCursor::writeUint32(uint32 i)
{
    m_Uint32 = i;
    advanceState(array("u", strlen("u")), Uint32);
}

void ArgumentList::WriteCursor::writeInt64(int64 i)
{
    m_Int64 = i;
    advanceState(array("x", strlen("x")), Int64);
}

void ArgumentList::WriteCursor::writeUint64(uint64 i)
{
    m_Uint64 = i;
    advanceState(array("t", strlen("t")), Uint64);
}

void ArgumentList::WriteCursor::writeDouble(double d)
{
    m_Double = d;
    advanceState(array("d", strlen("d")), Double);
}

void ArgumentList::WriteCursor::writeString(cstring string)
{
    m_String.begin = string.begin;
    m_String.length = string.length;
    advanceState(array("s", strlen("s")), String);
}

void ArgumentList::WriteCursor::writeObjectPath(cstring objectPath)
{
    m_String.begin = objectPath.begin;
    m_String.length = objectPath.length;
    advanceState(array("o", strlen("o")), ObjectPath);
}

void ArgumentList::WriteCursor::writeSignature(cstring signature)
{
    m_String.begin = signature.begin;
    m_String.length = signature.length;
    advanceState(array("g", strlen("g")), Signature);
}

void ArgumentList::WriteCursor::writeUnixFd(uint32 fd)
{
    m_Uint32 = fd;
    advanceState(array("h", strlen("h")), UnixFd);
}
