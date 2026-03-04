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

#include "arguments.h"
#include "arguments_p.h"

#include "basictypeio.h"
#include "error.h"
#include "malloccache.h"
#include "message.h"
#include "platform.h"
#include "stringtools.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stack>
#include <unordered_map>

const TypeInfo &typeInfo(char letterCode)
{
    assert(letterCode >= '(');
    static const TypeInfo low[2] = {
        { Arguments::BeginStruct,  8, false, false }, // (
        { Arguments::EndStruct,    1, false, false }  // )
    };
    if (letterCode <= ')') {
        return low[letterCode - '('];
    }
    assert(letterCode >= 'a' && letterCode <= '}');
    // entries for invalid letters are designed to be as inert as possible in the code using the data,
    // which may make it possible to catch errors at a common point with less special case code.
    static const TypeInfo high['}' - 'a' + 1] = {
        { Arguments::BeginArray,   4, false, false }, // a
        { Arguments::Boolean,      4, true,  false }, // b
        { Arguments::InvalidData,  1, true,  false }, // c
        { Arguments::Double,       8, true,  false }, // d
        { Arguments::InvalidData,  1, true,  false }, // e
        { Arguments::InvalidData,  1, true,  false }, // f
        { Arguments::Signature,    1, false, true  }, // g
        { Arguments::UnixFd,       4, true,  false }, // h
        { Arguments::Int32,        4, true,  false }, // i
        { Arguments::InvalidData,  1, true,  false }, // j
        { Arguments::InvalidData,  1, true,  false }, // k
        { Arguments::InvalidData,  1, true,  false }, // l
        { Arguments::InvalidData,  1, true,  false }, // m
        { Arguments::Int16,        2, true,  false }, // n
        { Arguments::ObjectPath,   4, false, true  }, // o
        { Arguments::InvalidData,  1, true,  false }, // p
        { Arguments::Uint16,       2, true,  false }, // q
        { Arguments::InvalidData,  1, true,  false }, // r
        { Arguments::String,       4, false, true  }, // s
        { Arguments::Uint64,       8, true,  false }, // t
        { Arguments::Uint32,       4, true,  false }, // u
        { Arguments::BeginVariant, 1, false, false }, // v
        { Arguments::InvalidData,  1, true,  false }, // w
        { Arguments::Int64,        8, true,  false }, // x
        { Arguments::Byte,         1, true,  false }, // y
        { Arguments::InvalidData,  1, true,  false }, // z
        { Arguments::BeginDict,    8, false, false }, // {
        { Arguments::InvalidData,  1, true,  false }, // |
        { Arguments::EndDict,      1, false, false }  // }
    };
    return high[letterCode - 'a'];
}

cstring printableState(Arguments::IoState state)
{
    if (state < Arguments::NotStarted || state >= Arguments::LastState) {
        return cstring();
    }
    static const char *strings[Arguments::LastState] = {
        "NotStarted",
        "Finished",
        "NeedMoreData",
        "InvalidData",
        "AnyData",
        "DictKey",
        "BeginArray",
        "EndArray",
        "BeginDict",
        "EndDict",
        "BeginStruct",
        "EndStruct",
        "BeginVariant",
        "EndVariant",
        "Boolean",
        "Byte",
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
#ifdef WITH_DICT_ENTRY
        ,
        "BeginDictEntry",
        "EndDictEntry"
#endif
    };
    return cstring(strings[state]);
}

// When using this to iterate over the reader, it will make an exact copy using the Writer.
// You need to do something only in states where something special should happen.
// To check errors, "simply" (sorry!) check the reader->state() and writer()->state().
// Note that you don't have to check the state before each element, it is fine to call
// read / write functions in error state, including with garbage data from the possibly
// invalid reader, and the reader / writer state will remain frozen in the state in which
// the first error occurred
// TODO: that text above belongs into a "Reader and Writer state / errors" explanation of the docs

// static
void Arguments::copyOneElement(Arguments::Reader *reader, Arguments::Writer *writer)
{
    switch(reader->state()) {
    case Arguments::BeginStruct:
        reader->beginStruct();
        writer->beginStruct();
        break;
    case Arguments::EndStruct:
        reader->endStruct();
        writer->endStruct();
        break;
    case Arguments::BeginVariant:
        reader->beginVariant();
        writer->beginVariant();
        break;
    case Arguments::EndVariant:
        reader->endVariant();
        writer->endVariant();
        break;
    case Arguments::BeginArray: {
        // Application note: to avoid handling arrays as primitive (where applicable), just don't
        // call this function in BeginArray state and do as in the else case.
        const Arguments::IoState primitiveType = reader->peekPrimitiveArray();
        if (primitiveType != BeginArray) { // InvalidData can't happen because the state *is* BeginArray
            const std::pair<Arguments::IoState, chunk> arrayData = reader->readPrimitiveArray();
            writer->writePrimitiveArray(arrayData.first, arrayData.second);
        } else {
            const bool hasData = reader->beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
            writer->beginArray(hasData ? Arguments::Writer::NonEmptyArray
                                       : Arguments::Writer::WriteTypesOfEmptyArray);
        }
        break; }
    case Arguments::EndArray:
        reader->endArray();
        writer->endArray();
        break;
    case Arguments::BeginDict: {
        const bool hasData = reader->beginDict(Arguments::Reader::ReadTypesOnlyIfEmpty);
        writer->beginDict(hasData ? Arguments::Writer::NonEmptyArray
                                    : Arguments::Writer::WriteTypesOfEmptyArray);
        break; }
    case Arguments::EndDict:
        reader->endDict();
        writer->endDict();
        break;
#ifdef WITH_DICT_ENTRY
    case Arguments::BeginDictEntry:
        reader->beginDictEntry();
        writer->beginDictEntry();
        break;
    case Arguments::EndDictEntry:
        reader->endDictEntry();
        writer->endDictEntry();
        break;
#endif
    case Arguments::Byte:
        writer->writeByte(reader->readByte());
        break;
    case Arguments::Boolean:
        writer->writeBoolean(reader->readBoolean());
        break;
    case Arguments::Int16:
        writer->writeInt16(reader->readInt16());
        break;
    case Arguments::Uint16:
        writer->writeUint16(reader->readUint16());
        break;
    case Arguments::Int32:
        writer->writeInt32(reader->readInt32());
        break;
    case Arguments::Uint32:
        writer->writeUint32(reader->readUint32());
        break;
    case Arguments::Int64:
        writer->writeInt64(reader->readInt64());
        break;
    case Arguments::Uint64:
        writer->writeUint64(reader->readUint64());
        break;
    case Arguments::Double:
        writer->writeDouble(reader->readDouble());
        break;
    case Arguments::String: {
        const cstring s = reader->readString();
        writer->writeString(s);
        break; }
    case Arguments::ObjectPath: {
        const cstring objectPath = reader->readObjectPath();
        writer->writeObjectPath(objectPath);
        break; }
    case Arguments::Signature: {
        const cstring signature = reader->readSignature();
        writer->writeSignature(signature);
        break; }
    case Arguments::UnixFd:
        writer->writeUnixFd(reader->readUnixFd());
        break;
    // special cases follow
    case Arguments::Finished:
        break; // You *probably* want to handle that one in the caller, but you don't have to
    case Arguments::NeedMoreData:
        break; // No way to handle that one here
    default:
        break; // dito
    }
}

thread_local static MallocCache<sizeof(Arguments::Private), 4> allocCache;

Arguments::Private::Private(const Private &other)
{
    initFrom(other);
}

Arguments::Private &Arguments::Private::operator=(const Private &other)
{
    if (this != &other) {
        initFrom(other);
    }
    return *this;
}

void Arguments::Private::initFrom(const Private &other)
{
    m_isByteSwapped = other.m_isByteSwapped;

    // make a deep copy
    // use only one malloced block for signature and main data - this saves one malloc and free
    // and also saves a pointer
    // (if it weren't for the Arguments(..., cstring signature, chunk data, ...) constructor
    //  we could save more size, and it would be very ugly, if we stored m_signature and m_data
    //  as offsets to m_memOwnership)
    m_memOwnership = nullptr;
    m_signature.length = other.m_signature.length;
    m_data.length = other.m_data.length;

    m_fileDescriptors = other.m_fileDescriptors;
    m_error = other.m_error;

    const uint32 alignedSigLength = other.m_signature.length ? align(other.m_signature.length + 1, 8) : 0;
    const uint32 fullLength = alignedSigLength + other.m_data.length;

    if (fullLength != 0) {
        // deep copy if there is any data
        m_memOwnership = reinterpret_cast<byte *>(malloc(fullLength));

        m_signature.ptr = reinterpret_cast<char *>(m_memOwnership);
        memcpy(m_signature.ptr, other.m_signature.ptr, other.m_signature.length + 1);
        uint32 bufferPos = other.m_signature.length + 1;
        zeroPad(reinterpret_cast<byte *>(m_signature.ptr), 8, &bufferPos);
        assert(bufferPos == alignedSigLength);

        if (other.m_data.length) {
            m_data.ptr = m_memOwnership + alignedSigLength;
            memcpy(m_data.ptr, other.m_data.ptr, other.m_data.length);
        } else {
            m_data.ptr = nullptr;
        }
    } else {
        m_signature.ptr = nullptr;
        m_data.ptr = nullptr;
    }
}

Arguments::Private::~Private()
{
    if (m_memOwnership) {
        free(m_memOwnership);
    }
}

Arguments::Arguments()
   : d(new(allocCache.allocate()) Private)
{
}

Arguments::Arguments(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped)
   : d(new(allocCache.allocate()) Private)
{
    d->m_isByteSwapped = isByteSwapped;
    d->m_memOwnership = memOwnership;
    d->m_signature = signature;
    d->m_data = data;
}

Arguments::Arguments(byte *memOwnership, cstring signature, chunk data,
                     std::vector<int> fileDescriptors, bool isByteSwapped)
   : d(new(allocCache.allocate()) Private)
{
    d->m_isByteSwapped = isByteSwapped;
    d->m_memOwnership = memOwnership;
    d->m_signature = signature;
    d->m_data = data;
    d->m_fileDescriptors = std::move(fileDescriptors);
}

Arguments::Arguments(Arguments &&other)
   : d(other.d)
{
    other.d = nullptr;
}

Arguments &Arguments::operator=(Arguments &&other)
{
    Arguments temp(std::move(other));
    std::swap(d, temp.d);
    return *this;
}

Arguments::Arguments(const Arguments &other)
   : d(nullptr)
{
    if (other.d) {
        d = new(allocCache.allocate()) Private(*other.d);
    }
}

Arguments &Arguments::operator=(const Arguments &other)
{
    if (d && other.d) {
        *d = *other.d;
    } else {
        Arguments temp(other);
        std::swap(d, temp.d);
    }
    return *this;
}

Arguments::~Arguments()
{
    if (d) {
        d->~Private();
        allocCache.free(d);
        d = nullptr;
    }
}

Error Arguments::error() const
{
    return d->m_error;
}

cstring Arguments::signature() const
{
    return d->m_signature;
}

chunk Arguments::data() const
{
    return d->m_data;
}

const std::vector<int> &Arguments::fileDescriptors() const
{
    return d->m_fileDescriptors;
}

bool Arguments::isByteSwapped() const
{
    return d->m_isByteSwapped;
}

static void printMaybeNilProlog(std::stringstream *out, const std::string &nestingPrefix, bool isNil,
                                const char *typeName)
{
    *out << nestingPrefix << typeName << ": ";
    if (isNil) {
        *out << "<nil>\n";
    }
}

template<typename T>
void printMaybeNil(std::stringstream *out, const std::string &nestingPrefix, bool isNil,
                   T value, const char *typeName)
{
    printMaybeNilProlog(out, nestingPrefix, isNil, typeName);
    if (!isNil) {
        *out << value << '\n';
    }
}

template<>
void printMaybeNil<cstring>(std::stringstream *out, const std::string &nestingPrefix, bool isNil,
                            cstring cstr, const char *typeName)
{
    printMaybeNilProlog(out, nestingPrefix, isNil, typeName);
    if (!isNil) {
        *out << '"' << toStdString(cstr) << "\"\n";
    }
}

static bool strEndsWith(const std::string &str, const std::string &ending)
{
    if (str.length() >= ending.length()) {
        return str.compare(str.length() - ending.length(), ending.length(), ending) == 0;
    } else {
        return false;
    }
}

std::string Arguments::prettyPrint() const
{
    Reader reader(*this);
    if (!reader.isValid()) {
        return std::string();
    }
    std::stringstream ret;
    std::string nestingPrefix;

    bool isDone = false;

    // Cache it, don't call Reader::isInsideEmptyArray() on every data element. This isn't really
    // a big deal for performance here, but in other situations it is, so set a good example :)
    bool inEmptyArray = false;

    while (!isDone) {
        // HACK use nestingPrefix to determine when we're switching from key to value - this can be done
        //      more cleanly with an aggregate stack if translation or similar makes this approach too ugly
        if (reader.isDictKey()) {
            if (strEndsWith(nestingPrefix, "V ")) {
                nestingPrefix.resize(nestingPrefix.size() - strlen("V "));
                assert(strEndsWith(nestingPrefix, "{ "));
            }
        }
        if (strEndsWith(nestingPrefix, "{ ")) {
            nestingPrefix += "K ";
        } else if (strEndsWith(nestingPrefix, "K ")) {
            nestingPrefix.replace(nestingPrefix.size() - strlen("K "), strlen("V "), "V ");
        }
        switch(reader.state()) {
        case Arguments::Finished:
            assert(nestingPrefix.empty());
            isDone = true;
            break;
        case Arguments::BeginStruct:
            reader.beginStruct();
            ret << nestingPrefix << "begin struct\n";
            nestingPrefix += "( ";
            break;
        case Arguments::EndStruct:
            reader.endStruct();
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end struct\n";
            break;
        case Arguments::BeginVariant:
            reader.beginVariant();
            ret << nestingPrefix << "begin variant\n";
            nestingPrefix += "* ";
            break;
        case Arguments::EndVariant:
            reader.endVariant();
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end variant\n";
            break;
        case Arguments::BeginArray:
            if (reader.peekPrimitiveArray() == Arguments::Byte) {
                // print byte arrays in a more space-efficient format
                const std::pair<Arguments::IoState, chunk> bytes = reader.readPrimitiveArray();
                assert(bytes.first == Arguments::Byte);
                assert(bytes.second.length > 0);
                inEmptyArray = reader.isInsideEmptyArray(); // Maybe not necessary, but safe
                ret << nestingPrefix << "array of bytes [ " << uint(bytes.second.ptr[0]);
                for (uint32 i = 1; i < bytes.second.length; i++) {
                    ret << ", " << uint(bytes.second.ptr[i]);
                }
                ret << " ]\n";
            } else {
                inEmptyArray = !reader.beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
                ret << nestingPrefix << "begin array\n";
                nestingPrefix += "[ ";
            }
            break;
        case Arguments::EndArray:
            reader.endArray();
            inEmptyArray = reader.isInsideEmptyArray();
            nestingPrefix.resize(nestingPrefix.size() - 2);
            ret << nestingPrefix << "end array\n";
            break;
        case Arguments::BeginDict: {
            inEmptyArray = !reader.beginDict(Arguments::Reader::ReadTypesOnlyIfEmpty);
            ret << nestingPrefix << "begin dict\n";
            nestingPrefix += "{ ";
            break; }
#ifdef WITH_DICT_ENTRY
        // We *could* use those states to be a bit more efficient than with calling isDictKey() all
        // the time, but let's keep it simple, and WITH_DICT_ENTRY as a non-default configuration.
        case Arguments::BeginDictEntry:
            reader.beginDictEntry();
            break;
        case Arguments::EndDictEntry:
            reader.endDictEntry();
            break;
#endif
        case Arguments::EndDict:
            reader.endDict();
            inEmptyArray = reader.isInsideEmptyArray();
            nestingPrefix.resize(nestingPrefix.size() - strlen("{ V "));
            ret << nestingPrefix << "end dict\n";
            break;
        case Arguments::Boolean: {
            bool b = reader.readBoolean();
            ret << nestingPrefix << "bool: ";
            if (inEmptyArray) {
                ret << "<nil>";
            } else {
                ret << (b ? "true" : "false");
            }
            ret << '\n';
            break; }
        case Arguments::Byte:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, int(reader.readByte()), "byte");
            break;
        case Arguments::Int16:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readInt16(), "int16");
            break;
        case Arguments::Uint16:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readUint16(), "uint16");
            break;
        case Arguments::Int32:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readInt32(), "int32");
            break;
        case Arguments::Uint32:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readUint32(), "uint32");
            break;
        case Arguments::Int64:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readInt64(), "int64");
            break;
        case Arguments::Uint64:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readUint64(), "uint64");
            break;
        case Arguments::Double:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readDouble(), "double");
            break;
        case Arguments::String:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readString(), "string");
            break;
        case Arguments::ObjectPath:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readObjectPath(), "object path");
            break;
        case Arguments::Signature:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readSignature(), "type signature");
            break;
        case Arguments::UnixFd:
            printMaybeNil(&ret, nestingPrefix, inEmptyArray, reader.readUnixFd(), "file descriptor");
            break;
        case Arguments::InvalidData:
        case Arguments::NeedMoreData:
        default: {
            return std::string("<error: ") +
                   toStdString(reader.stateString()) + ">\n";
            break; }
        }
    }
    return ret.str();
}

static void chopFirst(cstring *s)
{
    s->ptr++;
    s->length--;
}

// static
bool Arguments::isStringValid(cstring string)
{
    if (!string.ptr || string.length + 1 >= MaxArrayLength || string.ptr[string.length] != 0) {
        return false;
    }
    // check that there are no embedded nulls, exploiting the highly optimized strlen...
    return strlen(string.ptr) == string.length;
}

static inline bool isObjectNameLetter(char c)
{
    return likely((c >= 'a' && c <= 'z') || c == '_' || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
}

// static
bool Arguments::isObjectPathValid(cstring path)
{
    if (!path.ptr || path.length + 1 >= MaxArrayLength || path.ptr[path.length] != 0) {
        return false;
    }
    char prevLetter = path.ptr[0];
    if (prevLetter != '/') {
        return false;
    }
    if (path.length == 1) {
        return true; // "/" special case
    }
    for (uint32 i = 1; i < path.length; i++) {
        char currentLetter = path.ptr[i];
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
bool Arguments::isObjectPathElementValid(cstring pathElement)
{
    if (!pathElement.length) {
        return false;
    }
    for (uint32 i = 0; i < pathElement.length; i++) {
        if (!isObjectNameLetter(pathElement.ptr[i])) {
            return false;
        }
    }
    return true;
}

static bool parseBasicType(cstring *s)
{
    // ### not checking if zero-terminated
    assert(s->ptr);
    if (s->length == 0) {
        return false;
    }
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
        chopFirst(s);
        return true;
    default:
        return false;
    }
}

bool parseSingleCompleteType(cstring *s, Nesting *nest)
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
        if (!s->length || *s->ptr != ')' || isEmptyStruct) {
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
        if (*s->ptr == '{') { // an "array of dict entries", i.e. a dict
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
            if (!s->length || *s->ptr != '}') {
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
bool Arguments::isSignatureValid(cstring signature, SignatureType type)
{
    Nesting nest;
    if (!signature.ptr || signature.length > 255 || signature.ptr[signature.length] != 0) {
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
    // All aggregates must be closed at the end; if one of these assertions triggers the parsing code is
    // broken (should have detected the problem earlier and returned false).
    assert(!nest.array);
    assert(!nest.paren);
    assert(!nest.variant);
    return true;
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
        chopFirst(s);
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
        ret.push_back(FerOp::End);
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
