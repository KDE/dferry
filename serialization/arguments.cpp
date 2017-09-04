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

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

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
    case 'h':
        chopFirst(s);
        return true;
    default:
        return false;
    }
}

bool parseSingleCompleteType(cstring *s, Nesting *nest)
{
    assert(s->ptr);
    // ### not cheching if zero-terminated

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
    if (!signature.ptr || signature.ptr[signature.length] != 0) {
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
