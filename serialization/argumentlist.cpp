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

class ArgumentList::Private
{
public:
    Private()
       : m_isByteSwapped(false),
         m_readerCount(0),
         m_hasWriter(false)
    {}

    int m_isByteSwapped;
    int m_readerCount;
    bool m_hasWriter;
    cstring m_signature;
    chunk m_data;
};

// Macros are really ugly, but here every use saves three lines, and it's nice to be able to write
// "data is good if X" instead of "data is bad if !X". That stuff should end up the same after
// optimization anyway.
// funny condition to avoid the dangling-else problem
#define VALID_IF(cond) if (likely(cond)) {} else { m_state = InvalidData; return; }

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

static cstring printableState(ArgumentList::IoState state)
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
   : d(new Private)
{
}

ArgumentList::ArgumentList(cstring signature, chunk data, bool isByteSwapped)
   : d(new Private)
{
    d->m_isByteSwapped = isByteSwapped;
    d->m_signature = signature;
    d->m_data = data;
}

ArgumentList::ArgumentList(ArgumentList &&other)
   : d(other.d)
{
    other.d = nullptr;
}

ArgumentList &ArgumentList::operator=(ArgumentList &&other)
{
    if (this != &other) {
        delete d;
        d = other.d;
        other.d = nullptr;
    }
    return *this;
}

ArgumentList::ArgumentList(const ArgumentList &other)
{
    d = new Private(*other.d);
}

void ArgumentList::operator=(const ArgumentList &other)
{
    if (&other != this) {
        *d = *other.d; // ### unsafe, it contains pointers itself. really needs a deep copy.
    }
}

ArgumentList::~ArgumentList()
{
    delete d;
    d = 0;
}

bool ArgumentList::isReading() const
{
    return d->m_readerCount;
}

bool ArgumentList::isWriting() const
{
    return d->m_hasWriter;
}

cstring ArgumentList::signature() const
{
    return d->m_signature;
}

chunk ArgumentList::data() const
{
    return d->m_data;
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
    Reader reader(*this);
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

bool ArgumentList::beginRead() const
{
    const bool ret = !d->m_hasWriter;
    if (ret) {
        d->m_readerCount++;
    }
    return ret;
}

bool ArgumentList::beginWrite()
{
    const bool ret = !d->m_readerCount && !d->m_hasWriter;
    if (ret) {
        d->m_hasWriter = true;
    }
    return ret;
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
    // check that there are no embedded nulls, exploiting the highly optimized strlen...
    return strlen(reinterpret_cast<char *>(string.begin)) == string.length;
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

class ArgumentList::Reader::Private
{
public:
    Private()
       : m_argList(nullptr),
         m_signaturePosition(-1),
         m_dataPosition(0),
         m_nilArrayNesting(0)
    {}

    const ArgumentList *m_argList;
    Nesting m_nesting;
    cstring m_signature;
    chunk m_data;
    int m_signaturePosition;
    int m_dataPosition;
    int m_nilArrayNesting; // this keeps track of how many nil arrays we are in

    struct ArrayInfo
    {
        uint32 dataEnd; // one past the last data byte of the array
        uint32 containedTypeBegin; // to rewind when reading the next element
    };

    struct VariantInfo
    {
        podCstring prevSignature;     // a variant switches the currently parsed signature, so we
        uint32 prevSignaturePosition; // need to store the old signature and parse position.
    };

    // for structs, we don't need to know more than that we are in a struct

    struct AggregateInfo
    {
        IoState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
        union {
            ArrayInfo arr;
            VariantInfo var;
        };
    };

    // this keeps track of which aggregates we are currently in
    std::vector<AggregateInfo> m_aggregateStack;
};

ArgumentList::Reader::Reader(const ArgumentList &al)
   : d(new Private),
     m_state(NotStarted)
{
    if (al.beginRead()) {
        d->m_argList = &al;
    }

    VALID_IF(d->m_argList);
    d->m_signature = d->m_argList->d->m_signature;
    d->m_data = d->m_argList->d->m_data;
    VALID_IF(ArgumentList::isSignatureValid(d->m_signature));
    advanceState();
}

ArgumentList::Reader::Reader(Reader &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = 0;
}

void ArgumentList::Reader::operator=(Reader &&other)
{
    if (&other == this) {
        return;
    }
    delete d;
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = 0;
}

ArgumentList::Reader::~Reader()
{
    if (d->m_argList) {
        d->m_argList->d->m_readerCount -= 1;
    }
    delete d;
    d = 0;
}

bool ArgumentList::Reader::isValid() const
{
    return d->m_argList;
}

cstring ArgumentList::Reader::stateString() const
{
    return printableState(m_state);
}

void ArgumentList::Reader::replaceData(chunk data)
{
    VALID_IF(data.length >= d->m_dataPosition);

    ptrdiff_t offset = data.begin - d->m_data.begin;

    // fix up saved signatures on the aggregate stack except for the first, which is not contained in m_data
    bool isOriginalSignature = true;
    const int size = d->m_aggregateStack.size();
    for (int i = 0; i < size; i++) {
        if (d->m_aggregateStack[i].aggregateType == BeginVariant) {
            if (isOriginalSignature) {
                isOriginalSignature = false;
            } else {
                d->m_aggregateStack[i].var.prevSignature.begin += offset;
            }
        }
    }
    if (!isOriginalSignature) {
        d->m_signature.begin += offset;
    }

    d->m_data = data;
    if (m_state == NeedMoreData) {
        advanceState();
    }
}

struct TypeInfo
{
    ArgumentList::IoState state() const { return static_cast<ArgumentList::IoState>(_state); }
    byte _state;
    byte alignment : 6;
    bool isPrimitive : 1;
    bool isString : 1;
};

static const TypeInfo &typeInfo(byte letterCode)
{
    assert(letterCode >= '(');
    static const TypeInfo low[2] = {
        { ArgumentList::BeginStruct,  8, false, false }, // (
        { ArgumentList::EndStruct,    1, false, false }  // )
    };
    if (letterCode <= ')') {
        return low[letterCode - '('];
    }
    assert(letterCode >= 'a' && letterCode <= '}');
    // entries for invalid letters are designed to be as inert as possible in the code using the data,
    // which may make it possible to catch errors at a common point with less special case code.
    static const TypeInfo high['}' - 'a' + 1] = {
        { ArgumentList::BeginArray,   4, false, false }, // a
        { ArgumentList::Boolean,      4, true,  false }, // b
        { ArgumentList::InvalidData,  1, true,  false }, // c
        { ArgumentList::Double,       8, true,  false }, // d
        { ArgumentList::InvalidData,  1, true,  false }, // e
        { ArgumentList::InvalidData,  1, true,  false }, // f
        { ArgumentList::Signature,    1, false, true  }, // g
        { ArgumentList::UnixFd,       4, true,  false }, // h
        { ArgumentList::Int32,        4, true,  false }, // i
        { ArgumentList::InvalidData,  1, true,  false }, // j
        { ArgumentList::InvalidData,  1, true,  false }, // k
        { ArgumentList::InvalidData,  1, true,  false }, // l
        { ArgumentList::InvalidData,  1, true,  false }, // m
        { ArgumentList::Int16,        2, true,  false }, // n
        { ArgumentList::ObjectPath,   4, false, true  }, // o
        { ArgumentList::InvalidData,  1, true,  false }, // p
        { ArgumentList::Uint16,       2, true,  false }, // q
        { ArgumentList::InvalidData,  1, true,  false }, // r
        { ArgumentList::String,       4, false, true  }, // s
        { ArgumentList::Uint64,       8, true,  false }, // t
        { ArgumentList::Uint32,       4, true,  false }, // u
        { ArgumentList::BeginVariant, 1, false, false }, // v
        { ArgumentList::InvalidData,  1, true,  false }, // w
        { ArgumentList::Int64,        8, true,  false }, // x
        { ArgumentList::Byte,         1, true,  false }, // y
        { ArgumentList::InvalidData,  1, true,  false }, // z
        { ArgumentList::BeginDict,    8, false, false }, // {
        { ArgumentList::InvalidData,  1, true,  false }, // |
        { ArgumentList::EndDict,      1, false, false }  // }
    };
    return high[letterCode - 'a'];
}

ArgumentList::IoState ArgumentList::Reader::doReadPrimitiveType()
{
    switch(m_state) {
    case Byte:
        m_u.Byte = d->m_data.begin[d->m_dataPosition];
        break;
    case Boolean: {
        uint32 num = basic::readUint32(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        m_u.Boolean = num == 1;
        if (num > 1) {
            return InvalidData;
        }
        break; }
    case Int16:
        m_u.Int16 = basic::readInt16(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Uint16:
        m_u.Uint16 = basic::readUint16(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Int32:
        m_u.Int32 = basic::readInt32(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Uint32:
        m_u.Uint32 = basic::readUint32(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Int64:
        m_u.Int64 = basic::readInt64(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Uint64:
        m_u.Uint64 = basic::readUint64(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case Double:
        m_u.Double = basic::readDouble(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        break;
    case UnixFd: {
        uint32 index = basic::readUint32(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
        uint32 ret; // TODO use index to retrieve the actual file descriptor
        m_u.Uint32 = ret;
        break; }
    default:
        assert(false);
        return InvalidData;
    }
    return m_state;
}

ArgumentList::IoState ArgumentList::Reader::doReadString(int lengthPrefixSize)
{
    uint32 stringLength = 1;
    if (lengthPrefixSize == 1) {
        stringLength += d->m_data.begin[d->m_dataPosition];
    } else {
        stringLength += basic::readUint32(d->m_data.begin + d->m_dataPosition,
                                          d->m_argList->d->m_isByteSwapped);
    }
    d->m_dataPosition += lengthPrefixSize;
    if (unlikely(d->m_dataPosition + stringLength > d->m_data.length)) {
        return NeedMoreData;
    }
    m_u.String.begin = d->m_data.begin + d->m_dataPosition;
    m_u.String.length = stringLength - 1; // terminating null is not counted
    d->m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_u.String.begin, m_u.String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_u.String.begin, m_u.String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_u.String.begin, m_u.String.length));
    }
    if (unlikely(!isValidString)) {
        return InvalidData;
    }
    return m_state;
}

void ArgumentList::Reader::advanceState()
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
    // can't do the following because a dict is one aggregate in our counting, but two according to
    // the spec: an array (one) containing dict entries (two)
    // assert(d->m_nesting.total() == d->m_aggregateStack.size());
    assert((d->m_nesting.total() == 0) == d->m_aggregateStack.empty());
    assert(d->m_signaturePosition < d->m_signature.length);

    const int savedSignaturePosition = d->m_signaturePosition;
    const int savedDataPosition = d->m_dataPosition;

    d->m_signaturePosition++;

    // check if we are about to close any aggregate or even the whole argument list
    if (d->m_aggregateStack.empty()) {
        if (d->m_signaturePosition >= d->m_signature.length) {
            m_state = Finished;
            return;
        }
    } else {
        const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginStruct:
            break; // handled later by TypeInfo knowing ')' -> EndStruct
        case BeginVariant:
            if (d->m_signaturePosition >= d->m_signature.length) {
                m_state = EndVariant;
                d->m_nesting.endVariant();
                const Private::VariantInfo &variantInfo = aggregateInfo.var;
                d->m_signature.begin = variantInfo.prevSignature.begin;
                d->m_signature.length = variantInfo.prevSignature.length;
                d->m_signaturePosition = variantInfo.prevSignaturePosition;
                d->m_aggregateStack.pop_back();
                return;
            }
            break;
        case BeginDict:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 2) {
                m_state = NextDictEntry;
                return;
            }
            break;
        case BeginArray:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                m_state = NextArrayEntry;
                return;
            }
            break;
        default:
            break;
        }
    }

    // for aggregate types, ty.alignment is just the alignment.
    // for primitive types, it's also the actual size.
    const TypeInfo ty = typeInfo(d->m_signature.begin[d->m_signaturePosition]);
    m_state = ty.state();

    if (unlikely(m_state == InvalidData)) {
        return;
    }

    // check if we have enough data for the next type, and read it
    // if we're in a nil array, we are iterating only over the types without reading any data

    if (likely(!d->m_nilArrayNesting)) {
        int padStart = d->m_dataPosition;
        d->m_dataPosition = align(d->m_dataPosition, ty.alignment);
        if (unlikely(d->m_dataPosition > d->m_data.length)) {
            goto out_needMoreData;
        }
        VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition));

        if (ty.isPrimitive || ty.isString) {
            if (unlikely(d->m_dataPosition + ty.alignment > d->m_data.length)) {
                goto out_needMoreData;
            }

            if (ty.isPrimitive) {
                m_state = doReadPrimitiveType();
                d->m_dataPosition += ty.alignment;
            } else {
                m_state = doReadString(ty.alignment);
                if (unlikely(m_state == NeedMoreData)) {
                    goto out_needMoreData;
                }
            }
            return;
        }
    } else {
        if (ty.isPrimitive || ty.isString) {
            return; // nothing to do! (readFoo() will return "random" data, so don't use that data!)
        }
    }

    // now the interesting part: aggregates

    Private::AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        VALID_IF(d->m_nesting.beginParen());
        aggregateInfo.aggregateType = BeginStruct;
        d->m_aggregateStack.push_back(aggregateInfo);
        break;
    case EndStruct:
        d->m_nesting.endParen();
        if (!d->m_aggregateStack.size() || d->m_aggregateStack.back().aggregateType != BeginStruct) {
            assert(false); // should never happen due to the pre-validated signature
        }
        d->m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        cstring signature;
        if (unlikely(d->m_nilArrayNesting)) {
            static const char *emptyString = "";
            signature = cstring(emptyString, 0);
        } else {
            if (unlikely(d->m_dataPosition >= d->m_data.length)) {
                goto out_needMoreData;
            }
            signature.length = d->m_data.begin[d->m_dataPosition++];
            signature.begin = d->m_data.begin + d->m_dataPosition;
            d->m_dataPosition += signature.length + 1;
            if (unlikely(d->m_dataPosition > d->m_data.length)) {
                goto out_needMoreData;
            }
            VALID_IF(ArgumentList::isSignatureValid(signature, ArgumentList::VariantSignature));
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        VALID_IF(d->m_nesting.beginVariant());

        aggregateInfo.aggregateType = BeginVariant;
        Private::VariantInfo &variantInfo = aggregateInfo.var;
        variantInfo.prevSignature.begin = d->m_signature.begin;
        variantInfo.prevSignature.length = d->m_signature.length;
        variantInfo.prevSignaturePosition = d->m_signaturePosition;
        d->m_aggregateStack.push_back(aggregateInfo);
        d->m_signature = signature;
        d->m_signaturePosition = -1; // because we increment d->m_signaturePosition before reading a char
        break; }

    case BeginArray: {
        uint32 arrayLength = 0;
        if (likely(!d->m_nilArrayNesting)) {
            if (unlikely(d->m_dataPosition + 4 > d->m_data.length)) {
                goto out_needMoreData;
            }
            static const int maxArrayDataLength = 67108864; // from the spec
            arrayLength = basic::readUint32(d->m_data.begin + d->m_dataPosition, d->m_argList->d->m_isByteSwapped);
            VALID_IF(arrayLength <= maxArrayDataLength);
            d->m_dataPosition += 4;
        }

        const TypeInfo firstElementTy = typeInfo(d->m_signature.begin[d->m_signaturePosition + 1]);

        m_state = firstElementTy.state() == BeginDict ? BeginDict : BeginArray;
        aggregateInfo.aggregateType = m_state;
        Private::ArrayInfo &arrayInfo = aggregateInfo.arr; // also used for dict

        // ### are we supposed to align d->m_dataPosition if the array is empty?
        if (likely(!d->m_nilArrayNesting)) {
            int padStart = d->m_dataPosition;
            d->m_dataPosition = align(d->m_dataPosition, firstElementTy.alignment);
            VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition));
            arrayInfo.dataEnd = d->m_dataPosition + arrayLength;
            if (unlikely(arrayInfo.dataEnd > d->m_data.length)) {
                // NB: do not clobber (the unsaved) nesting before potentially going to out_needMoreData!
                goto out_needMoreData;
            }
        }
        VALID_IF(d->m_nesting.beginArray());
        if (firstElementTy.state() == BeginDict) {
            d->m_signaturePosition++;
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen());
        }

        // position at the 'a' or '{' because we increment d->m_signaturePosition before reading a char
        arrayInfo.containedTypeBegin = d->m_signaturePosition;
        if (!arrayLength) {
            d->m_nilArrayNesting++;
        }

        d->m_aggregateStack.push_back(aggregateInfo);
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    // we only start an array when the data for it has fully arrived (possible due to the length
    // prefix), so if we still run out of data in an array the input is inconsistent.
    VALID_IF(!d->m_nesting.array);
    m_state = NeedMoreData;
    d->m_signaturePosition = savedSignaturePosition;
    d->m_dataPosition = savedDataPosition;
}

void ArgumentList::Reader::advanceStateFrom(IoState expectedState)
{
    // Calling this method could be replaced with using VALID_IF in the callers, but it currently
    // seems more conventient like this.
    VALID_IF(m_state == expectedState);
    advanceState();
}

void ArgumentList::Reader::beginArrayOrDict(bool isDict, bool *isEmpty)
{
    assert(!d->m_aggregateStack.empty());
    Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (isEmpty) {
        *isEmpty = d->m_nilArrayNesting;
    }

    if (unlikely(d->m_nilArrayNesting)) {
        if (!isEmpty) {
            // TODO this whole branch seems to be not covered by the tests
            // need to move d->m_signaturePosition to the end of the array signature *here* or it won't happen

            // fix up nesting and parse position before parsing the array signature
            if (isDict) {
                d->m_nesting.endParen();
                d->m_signaturePosition--; // it was moved ahead by one to skip the '{'
            }
            d->m_nesting.endArray();

            // parse the array signature in order to skip it
            // barring bugs, must have been too deep nesting inside variants if parsing fails
            cstring sigTail(d->m_signature.begin + d->m_signaturePosition, d->m_signature.length - d->m_signaturePosition);
            VALID_IF(parseSingleCompleteType(&sigTail, &d->m_nesting));
            // TODO tests don't seem to cover the next line - is it really correct and is d->m_signaturePosition
            // not overwritten (to the correct value) ?
            d->m_signaturePosition = d->m_signature.length - sigTail.length - 1;

            // un-fix up nesting
            d->m_nesting.beginArray();
            if (isDict) {
                d->m_nesting.beginParen();
            }
        }
    }
    m_state = isDict ? NextDictEntry : NextArrayEntry;
}

// TODO introduce an error state different from InvalidData when the wrong method is called
void ArgumentList::Reader::beginArray(bool *isEmpty)
{
    VALID_IF(m_state == BeginArray);
    beginArrayOrDict(false, isEmpty);
}

bool ArgumentList::Reader::nextArrayOrDictEntry(bool isDict)
{
    assert(!d->m_aggregateStack.empty());
    Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
    assert(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));
    Private::ArrayInfo &arrayInfo = aggregateInfo.arr;

    if (unlikely(d->m_nilArrayNesting)) {
        if (d->m_signaturePosition <= arrayInfo.containedTypeBegin) {
            // do one iteration to read the types; read the next type...
            advanceState();
            // theoretically, nothing can go wrong: the signature is pre-validated and we are not going
            // to read any data. also theoretically, there are no bugs in advanceState() :)
            return m_state != InvalidData;
        } else {
            // second iteration or skipping an empty array
            d->m_nilArrayNesting--;
        }
    } else {
        if (d->m_dataPosition < arrayInfo.dataEnd) {
            if (isDict) {
                d->m_dataPosition = align(d->m_dataPosition, 8); // align to dict entry
            }
            // rewind to start of contained type and read the type info there
            d->m_signaturePosition = arrayInfo.containedTypeBegin;
            advanceState();
            return m_state != InvalidData;
        }
    }
    // no more iterations
    m_state = isDict ? EndDict : EndArray;
    d->m_signaturePosition--; // this was increased in advanceState() before sending us here
    if (isDict) {
        d->m_nesting.endParen();
        d->m_signaturePosition++; // skip '}'
    }
    d->m_nesting.endArray();
    d->m_aggregateStack.pop_back();
    return false;
}

bool ArgumentList::Reader::nextArrayEntry()
{
    if (m_state == NextArrayEntry) {
        return nextArrayOrDictEntry(false);
    } else {
        m_state = InvalidData;
        return false;
    }
}

void ArgumentList::Reader::endArray()
{
    advanceStateFrom(EndArray);
}

void ArgumentList::Reader::beginDict(bool *isEmpty)
{
    VALID_IF(m_state == BeginDict);
    beginArrayOrDict(true, isEmpty);
}

bool ArgumentList::Reader::nextDictEntry()
{
    if (m_state == NextDictEntry) {
        return nextArrayOrDictEntry(true);
    } else {
        m_state = InvalidData;
        return false;
    }
}

void ArgumentList::Reader::endDict()
{
    advanceStateFrom(EndDict);
}

void ArgumentList::Reader::beginStruct()
{
    advanceStateFrom(BeginStruct);
}

void ArgumentList::Reader::endStruct()
{
    advanceStateFrom(EndStruct);
}

void ArgumentList::Reader::beginVariant()
{
    advanceStateFrom(BeginVariant);
}

void ArgumentList::Reader::endVariant()
{
    advanceStateFrom(EndVariant);
}

std::vector<ArgumentList::IoState> ArgumentList::Reader::aggregateStack() const
{
    const int count = d->m_aggregateStack.size();
    std::vector<IoState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(d->m_aggregateStack[i].aggregateType);
    }
    return ret;
}

class ArgumentList::Writer::Private
{
public:
    Private()
       : m_argList(nullptr),
         m_signature(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0),
         m_signaturePosition(0),
         m_data(reinterpret_cast<byte *>(malloc(InitialDataCapacity))),
         m_dataCapacity(InitialDataCapacity),
         m_dataPosition(0),
         m_nilArrayNesting(0)
    {}

    struct ArrayInfo
    {
        uint32 dataBegin; // one past the last data byte of the array
        uint32 containedTypeBegin; // to rewind when reading the next element
    };

    struct VariantInfo
    {
        podCstring prevSignature;     // a variant switches the currently parsed signature, so we
        uint32 prevSignaturePosition; // need to store the old signature and parse position.
        uint32 signatureIndex; // index in m_variantSignatures
    };

    struct StructInfo
    {
        uint32 containedTypeBegin;
    };

    struct AggregateInfo
    {
        IoState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
        union {
            ArrayInfo arr;
            VariantInfo var;
            StructInfo sct;
        };
    };

    // we don't know how long a variant signature is when starting the variant, but we have to
    // insert the signature into the datastream before the data. for that reason, we need a
    // postprocessing pass to insert the signatures when the stream is otherwise finished.

    // - need to split up long string data: all unaligned, arbitrary length
    // - need to fix up array length because even the length excluding the padding before and
    //   after varies with alignment of the first byte - for that the alignment parameters as
    //   described in plan.txt need to be solved (we can use a worst-case estimate at first)
    struct ElementInfo
    {
        ElementInfo(byte alignment, byte size_)
            : size(size_)
        {
            assert(alignment <= 8);
            static const byte alignLog[9] = { 0, 0, 1, 0, 2, 0, 0, 0, 3 };
            alignmentExponent = alignLog[alignment];
            assert(alignment < 2 || alignLog != 0);
        }
        byte alignment() { return 1 << alignmentExponent; }

        uint32 alignmentExponent : 2; // powers of 2, so 1, 2, 4, 8
        uint32 size : 6; // that's up to 63
        enum SizeCode {
            LargestSize = 60,
            ArrayLengthField,
            ArrayLengthEndMark,
            VariantSignature
        };
    };

    std::vector<ElementInfo> m_elements;
    std::vector<cstring> m_variantSignatures; // TODO; cstring might not work when reallocating data

    int m_dataElementsCountBeforeNilArray;
    int m_variantSignaturesCountBeforeNilArray;
    int m_dataPositionBeforeNilArray;

    ArgumentList *m_argList;
    NestingWithParenCounter m_nesting;
    cstring m_signature;
    int m_signaturePosition;

    enum {
        // got a linker error with static const int...
        InitialDataCapacity = 256
    };
    byte *m_data;
    int m_dataCapacity;
    int m_dataPosition;

    int m_nilArrayNesting;
    // this keeps track of which aggregates we are currently in
    std::vector<AggregateInfo> m_aggregateStack;
};

ArgumentList::Writer::Writer(ArgumentList *al)
   : d(new Private),
     m_state(InvalidData)
{
    if (al->beginWrite()) {
        d->m_argList = al;
        m_state = AnyData;
    }
}

ArgumentList::Writer::Writer(Writer &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = 0;
}

void ArgumentList::Writer::operator=(Writer &&other)
{
    if (&other == this) {
        return;
    }
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = 0;
}

ArgumentList::Writer::~Writer()
{
    if (d->m_argList) {
        assert(d->m_argList->d->m_hasWriter);
        d->m_argList->d->m_hasWriter = false;
    }
    free(d->m_data);
    d->m_data = 0;
    delete d;
    d = 0;
}

bool ArgumentList::Writer::isValid() const
{
    return d->m_argList;
}

cstring ArgumentList::Writer::stateString() const
{
    return printableState(m_state);
}

ArgumentList::IoState ArgumentList::Writer::doWritePrimitiveType(uint32 alignAndSize)
{
    d->m_dataPosition = align(d->m_dataPosition, alignAndSize);
    const uint32 newDataPosition = d->m_dataPosition + alignAndSize;
    if (unlikely(newDataPosition > d->m_dataCapacity)) {
        d->m_dataCapacity *= 2;
        d->m_data = reinterpret_cast<byte *>(realloc(d->m_data, d->m_dataCapacity));
    }

    switch(m_state) {
    case Byte:
        d->m_data[d->m_dataPosition] = m_u.Byte;
        break;
    case Boolean: {
        uint32 num = m_u.Boolean ? 1 : 0;
        basic::writeUint32(d->m_data + d->m_dataPosition, num);
        break; }
    case Int16:
        basic::writeInt16(d->m_data + d->m_dataPosition, m_u.Int16);
        break;
    case Uint16:
        basic::writeUint16(d->m_data + d->m_dataPosition, m_u.Uint16);
        break;
    case Int32:
        basic::writeInt32(d->m_data + d->m_dataPosition, m_u.Int32);
        break;
    case Uint32:
        basic::writeUint32(d->m_data + d->m_dataPosition, m_u.Uint32);
        break;
    case Int64:
        basic::writeInt64(d->m_data + d->m_dataPosition, m_u.Int64);
        break;
    case Uint64:
        basic::writeUint64(d->m_data + d->m_dataPosition, m_u.Uint64);
        break;
    case Double:
        basic::writeDouble(d->m_data + d->m_dataPosition, m_u.Double);
        break;
    case UnixFd: {
        uint32 index; // TODO = index of the FD we actually want to send
        basic::writeUint32(d->m_data + d->m_dataPosition, index);
        break; }
    default:
        assert(false);
        return InvalidData;
    }

    d->m_dataPosition = newDataPosition;
    d->m_elements.push_back(Private::ElementInfo(alignAndSize, alignAndSize));
    return m_state;
}

ArgumentList::IoState ArgumentList::Writer::doWriteString(int lengthPrefixSize)
{
    bool isValidString = false;
    if (m_state == String) {
        isValidString = ArgumentList::isStringValid(cstring(m_u.String.begin, m_u.String.length));
    } else if (m_state == ObjectPath) {
        isValidString = ArgumentList::isObjectPathValid(cstring(m_u.String.begin, m_u.String.length));
    } else if (m_state == Signature) {
        isValidString = ArgumentList::isSignatureValid(cstring(m_u.String.begin, m_u.String.length));
    }
    if (unlikely(!isValidString)) {
        return InvalidData;
    }

    d->m_dataPosition = align(d->m_dataPosition, lengthPrefixSize);
    const uint32 newDataPosition = d->m_dataPosition + lengthPrefixSize + m_u.String.length + 1;
    if (unlikely(newDataPosition > d->m_dataCapacity)) {
        while (newDataPosition > d->m_dataCapacity) {
            d->m_dataCapacity *= 2;
        }
        d->m_data = reinterpret_cast<byte *>(realloc(d->m_data, d->m_dataCapacity));
    }

    if (lengthPrefixSize == 1) {
        d->m_data[d->m_dataPosition] = m_u.String.length;
    } else {
        basic::writeUint32(d->m_data + d->m_dataPosition, m_u.String.length);
    }
    d->m_dataPosition += lengthPrefixSize;
    d->m_elements.push_back(Private::ElementInfo(lengthPrefixSize, lengthPrefixSize));

    memcpy(d->m_data + d->m_dataPosition, m_u.String.begin, m_u.String.length + 1);
    d->m_dataPosition = newDataPosition;
    for (uint32 l = m_u.String.length + 1; l; ) {
        uint32 chunkSize = std::min(l, uint32(Private::ElementInfo::LargestSize));
        d->m_elements.push_back(Private::ElementInfo(1, chunkSize));
        l -= chunkSize;
    }

    return m_state;
}

void ArgumentList::Writer::advanceState(chunk signatureFragment, IoState newState)
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
    // can't do the following because a dict is one aggregate in our counting, but two according to
    // the spec: an array (one) containing dict entries (two)
    // assert(d->m_nesting.total() == d->m_aggregateStack.size());
    assert((d->m_nesting.total() == 0) == d->m_aggregateStack.empty());

    m_state = newState;
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    if (signatureFragment.length) {
        TypeInfo ty = typeInfo(signatureFragment.begin[0]);
        alignment = ty.alignment;
        isPrimitiveType = ty.isPrimitive;
        isStringType = ty.isString;
    }

    bool isWritingSignature = d->m_signaturePosition == d->m_signature.length; // TODO correct?
    if (isWritingSignature) {
        // signature additions must conform to syntax
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= maxSignatureLength);

        if (!d->m_aggregateStack.empty()) {
            const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
            switch (aggregateInfo.aggregateType) {
            case BeginVariant:
                // arrays and variants may contain just one single complete type; note that this will
                // trigger only when not inside an aggregate inside the variant or (see below) array
                if (d->m_signaturePosition >= 1) {
                    VALID_IF(m_state == EndVariant);
                }
                break;
            case BeginArray:
                if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1) {
                    VALID_IF(m_state == EndArray);
                }
                break;
            case BeginDict:
                if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
                    VALID_IF(isPrimitiveType || isStringType);
                }
                // first type has been checked already, second must be present (checked in EndDict
                // state handler). no third type allowed.
                if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2) {
                    VALID_IF(m_state == EndDict);
                }
                break;
            default:
                break;
            }
        }

        // finally, extend the signature
        for (int i = 0; i < signatureFragment.length; i++) {
            d->m_signature.begin[d->m_signaturePosition++] = signatureFragment.begin[i];
        }
        d->m_signature.length += signatureFragment.length;
    } else {
        // signature must match first iteration (of an array/dict)
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= d->m_signature.length);
        // TODO need to apply special checks for state changes with no explicit signature char?
        // (end of array, end of variant)
        for (int i = 0; i < signatureFragment.length; i++) {
            VALID_IF(d->m_signature.begin[d->m_signaturePosition++] == signatureFragment.begin[i]);
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

    Private::AggregateInfo aggregateInfo;

    switch (m_state) {
    case BeginStruct:
        VALID_IF(d->m_nesting.beginParen());
        aggregateInfo.aggregateType = BeginStruct;
        aggregateInfo.sct.containedTypeBegin = d->m_signaturePosition;
        d->m_aggregateStack.push_back(aggregateInfo);
        d->m_elements.push_back(Private::ElementInfo(alignment, 0)); // align only
        break;
    case EndStruct:
        d->m_nesting.endParen();
        VALID_IF(!d->m_aggregateStack.empty());
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginStruct &&
                 d->m_signaturePosition > aggregateInfo.sct.containedTypeBegin + 1); // no empty structs
        d->m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        VALID_IF(d->m_nesting.beginVariant());
        aggregateInfo.aggregateType = BeginVariant;

        Private::VariantInfo &variantInfo = aggregateInfo.var;
        variantInfo.prevSignature.begin = d->m_signature.begin;
        variantInfo.prevSignature.length = d->m_signature.length;
        variantInfo.prevSignaturePosition = d->m_signaturePosition;
        variantInfo.signatureIndex = d->m_variantSignatures.size();

        d->m_aggregateStack.push_back(aggregateInfo);

        // arrange for finish() to take a signature from d->m_variantSignatures
        d->m_elements.push_back(Private::ElementInfo(1, Private::ElementInfo::VariantSignature));
        cstring str(reinterpret_cast<byte *>(malloc(maxSignatureLength + 1)), 0);
        d->m_variantSignatures.push_back(str);
        d->m_signature = str;
        d->m_signaturePosition = 0;
        break; }
    case EndVariant: {
        d->m_nesting.endVariant();
        VALID_IF(!d->m_aggregateStack.empty());
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginVariant);
        if (likely(!d->m_nilArrayNesting)) {
            // apparently, empty variants are not allowed. as an exception, in nil arrays they are
            // allowed for writing a type signature like "av" in the shortest possible way.
            VALID_IF(d->m_signaturePosition > 0);
        }
        d->m_signature.begin[d->m_signaturePosition] = '\0';


        Private::VariantInfo &variantInfo = aggregateInfo.var;
        assert(variantInfo.signatureIndex < d->m_variantSignatures.size());
        d->m_variantSignatures[variantInfo.signatureIndex].length = d->m_signaturePosition;
        assert(d->m_variantSignatures[variantInfo.signatureIndex].begin = d->m_signature.begin);

        d->m_signature.begin = variantInfo.prevSignature.begin;
        d->m_signature.length = variantInfo.prevSignature.length;
        d->m_signaturePosition = variantInfo.prevSignaturePosition;
        d->m_aggregateStack.pop_back();
        break; }

    case BeginDict:
    case BeginArray: {
        VALID_IF(d->m_nesting.beginArray());
        if (m_state == BeginDict) {
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen());
        }
        aggregateInfo.aggregateType = m_state;
        aggregateInfo.arr.dataBegin = d->m_dataPosition;
        aggregateInfo.arr.containedTypeBegin = d->m_signaturePosition;
        d->m_aggregateStack.push_back(aggregateInfo);

        d->m_elements.push_back(Private::ElementInfo(4, Private::ElementInfo::ArrayLengthField));
        if (m_state == BeginDict) {
            d->m_elements.push_back(Private::ElementInfo(structAlignment, 0)); // align to dict entry
            m_state = DictKey;
            return;
        }
        break; }

    case EndDict:
    case EndArray: {
        const bool isDict = m_state == EndDict;
        if (isDict) {
            d->m_nesting.endParen();
        }
        d->m_nesting.endArray();
        VALID_IF(!d->m_aggregateStack.empty());
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));
        VALID_IF(d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + (isDict ? 3 : 1));
        d->m_aggregateStack.pop_back();
        if (unlikely(d->m_nilArrayNesting)) {
            if (!--d->m_nilArrayNesting) {
                // last chance to erase data inside the empty array so it doesn't end up in the output
                auto sigBeginIt = d->m_variantSignatures.begin() + d->m_variantSignaturesCountBeforeNilArray;
                auto sigEndIt = d->m_variantSignatures.end();
                for (auto it = sigBeginIt; it < sigEndIt; ++it) {
                    free(it->begin);
                }
                d->m_variantSignatures.erase(sigBeginIt, sigEndIt);
                d->m_elements.erase(d->m_elements.begin() + d->m_dataElementsCountBeforeNilArray,
                                    d->m_elements.end());
                d->m_dataPosition = d->m_dataPositionBeforeNilArray;
            }
        }

        // ### not checking array size here, it may change by a few bytes in the final data stream
        //     due to alignment changes from a different start address
        d->m_elements.push_back(Private::ElementInfo(1, Private::ElementInfo::ArrayLengthEndMark));
        break; }
    default:
        VALID_IF(false);
        break;
    }

    m_state = AnyData;
}

void ArgumentList::Writer::beginArrayOrDict(bool isDict, bool isEmpty)
{
    isEmpty = isEmpty || d->m_nilArrayNesting;
    if (isEmpty) {
        if (!d->m_nilArrayNesting++) {
            // for simplictiy and performance in the fast path, we keep storing the data chunks and any
            // variant signatures written inside an empty array. when we close the array, though, we
            // throw away all that data and signatures and keep only changes in the signature containing
            // the topmost empty array.
            d->m_variantSignaturesCountBeforeNilArray = d->m_variantSignatures.size();
            d->m_dataElementsCountBeforeNilArray = d->m_elements.size() + 1; // +1 -> keep ArrayLengthField
            d->m_dataPositionBeforeNilArray = d->m_dataPosition;
        }
    }
    if (isDict) {
        advanceState(chunk("a{", strlen("a{")), BeginDict);
    } else {
        advanceState(chunk("a", strlen("a")), BeginArray);
    }
}

void ArgumentList::Writer::beginArray(bool isEmpty)
{
    beginArrayOrDict(false, isEmpty);
}

void ArgumentList::Writer::nextArrayOrDictEntry(bool isDict)
{
    // TODO sanity / syntax checks, data length check too?

    VALID_IF(!d->m_aggregateStack.empty());
    Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
    VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray));

    if (unlikely(d->m_nilArrayNesting)) {
        // allow one iteration to write the types
        VALID_IF(d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin);
    } else {
        if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
            // first iteration, nothing to do.
            // this is due to the feature that nextFooEntry() is not required before the first element.
        } else if (isDict) {
            // a dict must have a key and value
            VALID_IF(d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1);

            d->m_nesting.parenCount += 1; // for alignment accounting
            d->m_elements.push_back(Private::ElementInfo(structAlignment, 0)); // align to dict entry
        }
        // array case: we are not at start of contained type's signature, the array is at top of stack
        // -> we *are* at the end of a single complete type inside the array, syntax check passed
        d->m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
    }
}

void ArgumentList::Writer::nextArrayEntry()
{
    nextArrayOrDictEntry(false);
}

void ArgumentList::Writer::endArray()
{
    advanceState(chunk(), EndArray);
}

void ArgumentList::Writer::beginDict(bool isEmpty)
{
    beginArrayOrDict(true, isEmpty);
}

void ArgumentList::Writer::nextDictEntry()
{
    nextArrayOrDictEntry(true);
}

void ArgumentList::Writer::endDict()
{
    advanceState(chunk("}", strlen("}")), EndDict);
}

void ArgumentList::Writer::beginStruct()
{
    advanceState(chunk("(", strlen("(")), BeginStruct);
}

void ArgumentList::Writer::endStruct()
{
    advanceState(chunk(")", strlen(")")), EndStruct);
}

void ArgumentList::Writer::beginVariant()
{
    advanceState(chunk("v", strlen("v")), BeginVariant);
}

void ArgumentList::Writer::endVariant()
{
    advanceState(chunk(), EndVariant);
}

struct ArrayLengthField
{
    uint32 lengthFieldPosition;
    uint32 dataStartPosition;
};

void ArgumentList::Writer::finish()
{
    // what needs to happen here:
    // - check if the message can be closed - basically the aggregate stack must be empty
    // - assemble the message, inserting variant signatures and array lengths
    // - close the signature by adding the terminating null
    if (m_state == InvalidData) {
        return;
    }
    if (d->m_nesting.total() != 0) {
        m_state = InvalidData;
        return;
    }
    assert(!d->m_nilArrayNesting);
    assert(d->m_signaturePosition <= maxSignatureLength); // this should have been caught before

    // since we are only going to process already validated data here, no error in the realm of
    // defined behavior should be possible
    m_state = Finished;

    d->m_signature.begin[d->m_signaturePosition] = '\0';
    d->m_signature.length = d->m_signaturePosition;

    d->m_dataPosition = 0;

    // The maximum alignment blowup for naturally aligned types is just less than a factor of 2.
    // Structs and dict entries are always 8 byte aligned so they add a maximum blowup of 7 bytes
    // each (when they contain a byte).
    // Those estimates are very conservative (but easy!), so some space optimization is possible.
    const int bufferSize = d->m_dataCapacity * 2 + d->m_nesting.parenCount * 7;
    byte *buffer = reinterpret_cast<byte *>(malloc(std::max(int(Private::InitialDataCapacity), bufferSize)));
    int bufferPos = 0;
    uint32 count = d->m_elements.size();
    int variantSignatureIndex = 0;

    std::vector<ArrayLengthField> lengthFieldStack;

    for (uint32 i = 0; i < count; i++) {
        Private::ElementInfo ei = d->m_elements[i];
        if (ei.size <= Private::ElementInfo::LargestSize) {
            // copy data chunks while applying the proper alignment
            zeroPad(buffer, ei.alignment(), &bufferPos);
            // if !ei.size, it's alignment padding which does not apply to source data
            if (ei.size) {
                d->m_dataPosition = align(d->m_dataPosition, ei.alignment());
                memcpy(buffer + bufferPos, d->m_data + d->m_dataPosition, ei.size);
                bufferPos += ei.size;
                d->m_dataPosition += ei.size;
            }
        } else {
            // the value of ei.size has special meaning
            ArrayLengthField al;
            if (ei.size == Private::ElementInfo::ArrayLengthField) {
                // start of an array
                // reserve space for the array length prefix
                zeroPad(buffer, ei.alignment(), &bufferPos);
                al.lengthFieldPosition = bufferPos;
                bufferPos += 4;
                // array data starts aligned to the first array element
                zeroPad(buffer, d->m_elements[i + 1].alignment(), &bufferPos);
                al.dataStartPosition = bufferPos;
                lengthFieldStack.push_back(al);
            } else if (ei.size == Private::ElementInfo::ArrayLengthEndMark) {
                // end of an array - just put the now known array length in front of the array
                al = lengthFieldStack.back();
                basic::writeUint32(buffer + al.lengthFieldPosition, bufferPos - al.dataStartPosition);
                lengthFieldStack.pop_back();
            } else { // ei.size == Private::ElementInfo::VariantSignature
                // fill in signature (should already include trailing null)
                cstring signature = d->m_variantSignatures[variantSignatureIndex++];
                buffer[bufferPos++] = signature.length;
                memcpy(buffer + bufferPos, signature.begin, signature.length + 1);
                bufferPos += signature.length + 1;
                free(signature.begin);
            }
        }
    }
    assert(variantSignatureIndex == d->m_variantSignatures.size());
    assert(lengthFieldStack.empty());
    d->m_elements.clear();
    d->m_variantSignatures.clear();

    d->m_argList->d->m_signature = d->m_signature;
    d->m_argList->d->m_data = chunk(buffer, bufferPos);

    assert(d->m_argList->d->m_hasWriter);
    d->m_argList->d->m_hasWriter = false;
    d->m_argList = nullptr;
}

std::vector<ArgumentList::IoState> ArgumentList::Writer::aggregateStack() const
{
    const int count = d->m_aggregateStack.size();
    std::vector<IoState> ret;
    for (int i = 0; i < count; i++) {
        ret.push_back(d->m_aggregateStack[i].aggregateType);
    }
    return ret;
}

void ArgumentList::Writer::writeByte(byte b)
{
    m_u.Byte = b;
    advanceState(chunk("y", strlen("y")), Byte);
}

void ArgumentList::Writer::writeBoolean(bool b)
{
    m_u.Boolean = b;
    advanceState(chunk("b", strlen("b")), Boolean);
}

void ArgumentList::Writer::writeInt16(int16 i)
{
    m_u.Int16 = i;
    advanceState(chunk("n", strlen("n")), Int16);
}

void ArgumentList::Writer::writeUint16(uint16 i)
{
    m_u.Uint16 = i;
    advanceState(chunk("q", strlen("q")), Uint16);
}

void ArgumentList::Writer::writeInt32(int32 i)
{
    m_u.Int32 = i;
    advanceState(chunk("i", strlen("i")), Int32);
}

void ArgumentList::Writer::writeUint32(uint32 i)
{
    m_u.Uint32 = i;
    advanceState(chunk("u", strlen("u")), Uint32);
}

void ArgumentList::Writer::writeInt64(int64 i)
{
    m_u.Int64 = i;
    advanceState(chunk("x", strlen("x")), Int64);
}

void ArgumentList::Writer::writeUint64(uint64 i)
{
    m_u.Uint64 = i;
    advanceState(chunk("t", strlen("t")), Uint64);
}

void ArgumentList::Writer::writeDouble(double d)
{
    m_u.Double = d;
    advanceState(chunk("d", strlen("d")), Double);
}

void ArgumentList::Writer::writeString(cstring string)
{
    m_u.String.begin = string.begin;
    m_u.String.length = string.length;
    advanceState(chunk("s", strlen("s")), String);
}

void ArgumentList::Writer::writeObjectPath(cstring objectPath)
{
    m_u.String.begin = objectPath.begin;
    m_u.String.length = objectPath.length;
    advanceState(chunk("o", strlen("o")), ObjectPath);
}

void ArgumentList::Writer::writeSignature(cstring signature)
{
    m_u.String.begin = signature.begin;
    m_u.String.length = signature.length;
    advanceState(chunk("g", strlen("g")), Signature);
}

void ArgumentList::Writer::writeUnixFd(uint32 fd)
{
    m_u.Uint32 = fd;
    advanceState(chunk("h", strlen("h")), UnixFd);
}
