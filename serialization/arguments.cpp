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

// Maximum message length is a good upper bound for maximum Arguments data length. In order to limit
// excessive memory consumption in error cases and prevent integer overflow exploits, enforce a maximum
// data length already in Arguments.
enum {
    SpecMaxArrayLength = 67108864, // 64 MiB
    SpecMaxMessageLength = 134217728 // 128 MiB
};

static constexpr byte alignLog[9] = { 0, 0, 1, 0, 2, 0, 0, 0, 3 };

static constexpr byte alignmentLog2(uint32 alignment)
{
    // The following is not constexpr in C++14, and it hasn't triggered in ages
    // assert(alignment <= 8 && (alignment < 2 || alignLog[alignment] != 0));
    return alignLog[alignment];
}

static cstring printableState(Arguments::IoState state)
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

// helper to verify the max nesting requirements of the d-bus spec
struct Nesting
{
    Nesting() : array(0), paren(0), variant(0) {}
    static const int arrayMax = 32;
    static const int parenMax = 32;
    static const int totalMax = 64;

    bool beginArray() { array++; return likely(array <= arrayMax && total() <= totalMax); }
    void endArray() { assert(array >= 1); array--; }
    bool beginParen() { paren++; return likely(paren <= parenMax && total() <= totalMax); }
    void endParen() { assert(paren >= 1); paren--; }
    bool beginVariant() { variant++; return likely(total() <= totalMax); }
    void endVariant() { assert(variant >= 1); variant--; }
    uint32 total() { return array + paren + variant; }

    uint32 array;
    uint32 paren;
    uint32 variant;
};

class Arguments::Private
{
public:
    Private()
       : m_isByteSwapped(false),
         m_memOwnership(nullptr)
    {}

    Private(const Private &other);
    Private &operator=(const Private &other);
    void initFrom(const Private &other);
    ~Private();

    chunk m_data;
    bool m_isByteSwapped;
    byte *m_memOwnership;
    cstring m_signature;
    std::vector<int> m_fileDescriptors;
    Error m_error;
};

class Arguments::Reader::Private
{
public:
    Private()
       : m_args(nullptr),
         m_signaturePosition(uint32(-1)),
         m_dataPosition(0),
         m_nilArrayNesting(0)
    {}

    const Arguments *m_args;
    cstring m_signature;
    uint32 m_signaturePosition;
    chunk m_data;
    uint32 m_dataPosition;
    uint32 m_nilArrayNesting; // this keeps track of how many nil arrays we are in
    Error m_error;
    Nesting m_nesting;

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
#ifdef HAVE_BOOST
    boost::small_vector<AggregateInfo, 8> m_aggregateStack;
#else
    std::vector<AggregateInfo> m_aggregateStack;
#endif
};

class Arguments::Writer::Private
{
public:
    Private()
       : m_signaturePosition(0),
         m_data(reinterpret_cast<byte *>(malloc(InitialDataCapacity))),
         m_dataCapacity(InitialDataCapacity),
         m_dataPosition(SignatureReservedSpace),
         m_nilArrayNesting(0)
    {
        m_signature.ptr = reinterpret_cast<char *>(m_data + 1); // reserve a byte for length prefix
        m_signature.length = 0;
    }

    Private(const Private &other);
    void operator=(const Private &other);

    void reserveData(uint32 size)
    {
        if (likely(size <= m_dataCapacity)) {
            return;
        }
        uint32 newCapacity = m_dataCapacity;
        do {
            newCapacity *= 2;
        } while (size > newCapacity);

        byte *const oldDataPointer = m_data;
        m_data = reinterpret_cast<byte *>(realloc(m_data, newCapacity));
        m_signature.ptr += m_data - oldDataPointer;
        m_dataCapacity = newCapacity;
    }

    bool insideVariant()
    {
        return !m_queuedData.empty();
    }

    // We don't know how long a variant signature is when starting the variant, but we have to
    // insert the signature into the datastream before the data. For that reason, we need a
    // postprocessing pass to fix things up once the outermost variant is closed.
    // QueuedDataInfo stores enough information about data inside variants to be able to do
    // the patching up while respecting alignment and other requirements.
    struct QueuedDataInfo
    {
        constexpr QueuedDataInfo(byte alignment, byte size_)
            : alignmentExponent(alignmentLog2(alignment)),
              size(size_)
        {}
        byte alignment() const { return 1 << alignmentExponent; }

        byte alignmentExponent : 2; // powers of 2, so 1, 2, 4, 8
        byte size : 6; // that's up to 63
        enum SizeCode {
            LargestSize = 60,
            ArrayLengthField,
            ArrayLengthEndMark,
            VariantSignature
        };
    };

    // The parameter is not a QueuedDataInfo because the compiler doesn't seem to optimize away
    // QueuedDataInfo construction when insideVariant() is false, despite inlining.
    void maybeQueueData(byte alignment, byte size)
    {
        if (insideVariant()) {
            m_queuedData.push_back(QueuedDataInfo(alignment, size));
        }
    }

    // Caution: does not ensure that enough space is available!
    void appendBulkData(chunk data)
    {
        // Align only the first of the back-to-back data chunks - otherwise, when storing values which
        // are 8 byte aligned, the second half of an element straddling a chunk boundary
        // (QueuedDataInfo::LargestSize == 60) would start at an 8-byte aligned position (so 64)
        // instead of 60 where we want it in order to just write a contiguous block of data.
        memcpy(m_data + m_dataPosition, data.ptr, data.length);
        m_dataPosition += data.length;
        if (insideVariant()) {
            for (uint32 l = data.length; l; ) {
                uint32 chunkSize = std::min(l, uint32(QueuedDataInfo::LargestSize));
                m_queuedData.push_back(QueuedDataInfo(1, chunkSize));
                l -= chunkSize;
            }
        }
    }

    void alignData(uint32 alignment)
    {
        if (insideVariant()) {
            m_queuedData.push_back(QueuedDataInfo(alignment, 0));
        }
        zeroPad(m_data, alignment, &m_dataPosition);
    }

    uint32 m_dataElementsCountBeforeNilArray;
    uint32 m_dataPositionBeforeVariant;

    Nesting m_nesting;
    cstring m_signature;
    uint32 m_signaturePosition;

    byte *m_data;
    uint32 m_dataCapacity;
    uint32 m_dataPosition;

    int m_nilArrayNesting;
    std::vector<int> m_fileDescriptors;
    Error m_error;

    enum {
        InitialDataCapacity = 512,
        // max signature length (255) + length prefix(1) + null terminator(1), rounded up to multiple of 8
        // because that doesn't change alignment
        SignatureReservedSpace = 264
    };

#ifdef WITH_DICT_ENTRY
    enum DictEntryState : byte
    {
        RequireBeginDictEntry = 0,
        InDictEntry,
        RequireEndDictEntry,
        AfterEndDictEntry
    };
#endif
    struct ArrayInfo
    {
        uint32 containedTypeBegin; // to rewind when reading the next element
#ifdef WITH_DICT_ENTRY
        DictEntryState dictEntryState;
        uint32 lengthFieldPosition : 24;
#else
        uint32 lengthFieldPosition;
#endif
    };

    struct VariantInfo
    {
        // a variant switches the currently parsed signature, so we
        // need to store the old signature and parse position.
        uint32 prevSignatureOffset; // relative to m_data
        uint32 prevSignaturePosition;
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

    // this keeps track of which aggregates we are currently in
#ifdef HAVE_BOOST
    boost::small_vector<AggregateInfo, 8> m_aggregateStack;
#else
    std::vector<AggregateInfo> m_aggregateStack;
#endif
    std::vector<QueuedDataInfo> m_queuedData;
};

struct ArgAllocCaches
{
    MallocCache<sizeof(Arguments::Private), 4> argsPrivate;
    MallocCache<sizeof(Arguments::Writer::Private), 4> writerPrivate;
    MallocCache<sizeof(Arguments::Reader::Private), 4> readerPrivate;
};

thread_local static ArgAllocCaches allocCaches;

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

// Macros are icky, but here every use saves three lines.
// Funny condition to avoid the dangling-else problem.
#define VALID_IF(cond, errCode) if (likely(cond)) {} else { \
    m_state = InvalidData; d->m_error.setCode(errCode); return; }

static const int structAlignment = 8;

Arguments::Arguments()
   : d(new(allocCaches.argsPrivate.allocate()) Private)
{
}

Arguments::Arguments(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped)
   : d(new(allocCaches.argsPrivate.allocate()) Private)
{
    d->m_isByteSwapped = isByteSwapped;
    d->m_memOwnership = memOwnership;
    d->m_signature = signature;
    d->m_data = data;
}

Arguments::Arguments(byte *memOwnership, cstring signature, chunk data,
                     std::vector<int> fileDescriptors, bool isByteSwapped)
   : d(new(allocCaches.argsPrivate.allocate()) Private)
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
        d = new(allocCaches.argsPrivate.allocate()) Private(*other.d);
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
        allocCaches.argsPrivate.free(d);
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
    if (!string.ptr || string.length + 1 >= SpecMaxArrayLength || string.ptr[string.length] != 0) {
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
    if (!path.ptr || path.length + 1 >= SpecMaxArrayLength || path.ptr[path.length] != 0) {
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

static bool parseSingleCompleteType(cstring *s, Nesting *nest)
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

Arguments::Reader::Reader(const Arguments &al)
   : d(new(allocCaches.readerPrivate.allocate()) Private),
     m_state(NotStarted)
{
    d->m_args = &al;
    beginRead();
}

Arguments::Reader::Reader(const Message &msg)
   : d(new(allocCaches.readerPrivate.allocate()) Private),
     m_state(NotStarted)
{
    d->m_args = &msg.arguments();
    beginRead();
}

Arguments::Reader::Reader(Reader &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = 0;
}

void Arguments::Reader::operator=(Reader &&other)
{
    if (&other == this) {
        return;
    }
    if (d) {
        d->~Private();
        allocCaches.writerPrivate.free(d);
    }
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = 0;
}

Arguments::Reader::Reader(const Reader &other)
   : d(nullptr),
     m_state(other.m_state),
     m_u(other.m_u)
{
    if (other.d) {
        d = new Private(*other.d);
    }
}

void Arguments::Reader::operator=(const Reader &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    m_u = other.m_u;
    if (d && other.d) {
        *d = *other.d;
    } else {
        Reader temp(other);
        std::swap(d, temp.d);
    }
}

Arguments::Reader::~Reader()
{
    delete d;
    d = 0;
}

void Arguments::Reader::beginRead()
{
    VALID_IF(d->m_args, Error::NotAttachedToArguments);
    d->m_signature = d->m_args->d->m_signature;
    d->m_data = d->m_args->d->m_data;
    // as a slightly hacky optimizaton, we allow empty Argumentss to allocate no space for d->m_buffer.
    if (d->m_signature.length) {
        VALID_IF(Arguments::isSignatureValid(d->m_signature), Error::InvalidSignature);
    }
    advanceState();
}

bool Arguments::Reader::isValid() const
{
    return d->m_args;
}

Error Arguments::Reader::error() const
{
    return d->m_error;
}

cstring Arguments::Reader::stateString() const
{
    return printableState(m_state);
}

bool Arguments::Reader::isInsideEmptyArray() const
{
    return d->m_nilArrayNesting > 0;
}

cstring Arguments::Reader::currentSignature() const
{
    return d->m_signature;
}

cstring Arguments::Reader::currentSingleCompleteTypeSignature() const
{
    const uint32 startingLength = d->m_signature.length - d->m_signaturePosition;
    cstring sigCopy = { d->m_signature.ptr + d->m_signaturePosition, startingLength };
    Nesting nest;
    if (!parseSingleCompleteType(&sigCopy, &nest)) {
        // the signature should have been validated before, but e.g. in Finished state this may happen
        return cstring();
    }
    sigCopy.ptr = d->m_signature.ptr + d->m_signaturePosition;
    sigCopy.length = startingLength - sigCopy.length;
    return sigCopy;
}

void Arguments::Reader::replaceData(chunk data)
{
    VALID_IF(data.length >= d->m_dataPosition, Error::ReplacementDataIsShorter);

    ptrdiff_t offset = data.ptr - d->m_data.ptr;

    // fix up variant signature addresses occurring on the aggregate stack pointing into m_data;
    // don't touch the original (= call parameter, not variant) signature, which does not point into m_data.
    bool isMainSignature = true;
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        if (aggregate.aggregateType == BeginVariant) {
            if (isMainSignature) {
                isMainSignature = false;
            } else {
                aggregate.var.prevSignature.ptr += offset;
            }
        }
    }
    if (!isMainSignature) {
        d->m_signature.ptr += offset;
    }

    d->m_data = data;
    if (m_state == NeedMoreData) {
        advanceState();
    }
}

struct TypeInfo
{
    Arguments::IoState state() const { return static_cast<Arguments::IoState>(_state); }
    byte _state;
    byte alignment : 6;
    bool isPrimitive : 1;
    bool isString : 1;
};

static const TypeInfo &typeInfo(char letterCode)
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

static char letterForPrimitiveIoState(Arguments::IoState ios)
{
    if (ios < Arguments::Boolean || ios > Arguments::Double) {
        return  'c'; // a known invalid letter that won't trip up typeInfo()
    }
    static const char letters[] = {
        'b', // Boolean
        'y', // Byte
        'n', // Int16
        'q', // Uint16
        'i', // Int32
        'u', // Uint32
        'x', // Int64
        't', // Uint64
        'd'  // Double
    };
    return letters[size_t(ios) - size_t(Arguments::Boolean)]; // TODO do we need the casts?
}

void Arguments::Reader::doReadPrimitiveType()
{
    switch(m_state) {
    case Boolean: {
        uint32 num = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        m_u.Boolean = num == 1;
        VALID_IF(num <= 1, Error::MalformedMessageData);
        break; }
    case Byte:
        m_u.Byte = d->m_data.ptr[d->m_dataPosition];
        break;
    case Int16:
        m_u.Int16 = basic::readInt16(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Uint16:
        m_u.Uint16 = basic::readUint16(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Int32:
        m_u.Int32 = basic::readInt32(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Uint32:
        m_u.Uint32 = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Int64:
        m_u.Int64 = basic::readInt64(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Uint64:
        m_u.Uint64 = basic::readUint64(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case Double:
        m_u.Double = basic::readDouble(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        break;
    case UnixFd: {
        uint32 index = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
        if (!d->m_nilArrayNesting) {
            VALID_IF(index < d->m_args->d->m_fileDescriptors.size(), Error::MalformedMessageData);
            m_u.Int32 = d->m_args->d->m_fileDescriptors[index];
        } else {
            m_u.Int32 = InvalidFileDescriptor;
        }
        break; }
    default:
        assert(false);
        VALID_IF(false, Error::MalformedMessageData);
    }
}

void Arguments::Reader::doReadString(uint32 lengthPrefixSize)
{
    uint32 stringLength = 1;
    if (lengthPrefixSize == 1) {
        stringLength += d->m_data.ptr[d->m_dataPosition];
    } else {
        stringLength += basic::readUint32(d->m_data.ptr + d->m_dataPosition,
                                          d->m_args->d->m_isByteSwapped);
        VALID_IF(stringLength + 1 < SpecMaxArrayLength, Error::MalformedMessageData);
    }
    d->m_dataPosition += lengthPrefixSize;
    if (unlikely(d->m_dataPosition + stringLength > d->m_data.length)) {
        m_state = NeedMoreData;
        return;
    }
    m_u.String.ptr = reinterpret_cast<char *>(d->m_data.ptr) + d->m_dataPosition;
    m_u.String.length = stringLength - 1; // terminating null is not counted
    d->m_dataPosition += stringLength;
    bool isValidString = false;
    if (m_state == String) {
        isValidString = Arguments::isStringValid(cstring(m_u.String.ptr, m_u.String.length));
    } else if (m_state == ObjectPath) {
        isValidString = Arguments::isObjectPathValid(cstring(m_u.String.ptr, m_u.String.length));
    } else if (m_state == Signature) {
        isValidString = Arguments::isSignatureValid(cstring(m_u.String.ptr, m_u.String.length));
    }
    VALID_IF(isValidString, Error::MalformedMessageData);
}

void Arguments::Reader::advanceState()
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

    const uint32 savedSignaturePosition = d->m_signaturePosition;
    const uint32 savedDataPosition = d->m_dataPosition;

    d->m_signaturePosition++;
    assert(d->m_signaturePosition <= d->m_signature.length);

    // check if we are about to close any aggregate or even the whole argument list
    if (d->m_aggregateStack.empty()) {
        // TODO check if there is still data left, if so it's probably an error
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
                return;
            }
            break;
        case BeginArray:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin) {
                // End of current iteration; either there are more or the array ends
                const Private::ArrayInfo &arrayInfo = aggregateInfo.arr;
                if (likely(!d->m_nilArrayNesting) && d->m_dataPosition < arrayInfo.dataEnd) {
                    // rewind to start of contained type and read the type info there
                    d->m_signaturePosition = arrayInfo.containedTypeBegin;
                    break; // proceed immediately to reading the next element in the array
                }
                // TODO check that final data position is where it should be according to the
                // serialized array length (same in BeginDict!)
                VALID_IF(d->m_dataPosition == arrayInfo.dataEnd, Error::MalformedMessageData);
                m_state = EndArray;
                return;
            }
            break;
        case BeginDict:
            if (d->m_signaturePosition > aggregateInfo.arr.containedTypeBegin + 1) {
                // Almost like BeginArray, only differences are commented
                const Private::ArrayInfo &arrayInfo = aggregateInfo.arr;
                if (likely(!d->m_nilArrayNesting) && d->m_dataPosition < arrayInfo.dataEnd) {
                    d->m_dataPosition = align(d->m_dataPosition, 8); // align to dict entry
                    d->m_signaturePosition = arrayInfo.containedTypeBegin;
#ifdef WITH_DICT_ENTRY
                    d->m_signaturePosition--;
                    m_state = EndDictEntry;
                    m_u.Uint32 = 0; // meaning: more dict entries follow (state after next is BeginDictEntry)
                    return;
#endif
                    break;
                }
#ifdef WITH_DICT_ENTRY
                m_state = EndDictEntry;
                m_u.Uint32 = 1; // meaning: array end reached (state after next is EndDict)
                return;
#endif
                m_state = EndDict;
                return;
            }
            break;
        default:
            break;
        }
    }

    // for aggregate types, ty.alignment is just the alignment.
    // for primitive types, it's also the actual size.
    const TypeInfo ty = typeInfo(d->m_signature.ptr[d->m_signaturePosition]);
    m_state = ty.state();

    VALID_IF(m_state != InvalidData, Error::MalformedMessageData);

    // check if we have enough data for the next type, and read it
    // if we're in a nil array, we are iterating only over the types without reading any data

    if (likely(!d->m_nilArrayNesting)) {
        uint32 padStart = d->m_dataPosition;
        d->m_dataPosition = align(d->m_dataPosition, ty.alignment);
        if (unlikely(d->m_dataPosition > d->m_data.length)) {
            goto out_needMoreData;
        }
        VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition), Error::MalformedMessageData);

        if (ty.isPrimitive || ty.isString) {
            if (unlikely(d->m_dataPosition + ty.alignment > d->m_data.length)) {
                goto out_needMoreData;
            }

            if (ty.isPrimitive) {
                doReadPrimitiveType();
                d->m_dataPosition += ty.alignment;
            } else {
                doReadString(ty.alignment);
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

    switch (m_state) {
    case BeginStruct:
        VALID_IF(d->m_nesting.beginParen(), Error::MalformedMessageData);
        break;
    case EndStruct:
        if (!d->m_aggregateStack.size() || d->m_aggregateStack.back().aggregateType != BeginStruct) {
            assert(false); // should never happen due to the pre-validated signature
        }
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
            signature.length = d->m_data.ptr[d->m_dataPosition++];
            signature.ptr = reinterpret_cast<char *>(d->m_data.ptr) + d->m_dataPosition;
            d->m_dataPosition += signature.length + 1;
            if (unlikely(d->m_dataPosition > d->m_data.length)) {
                goto out_needMoreData;
            }
            VALID_IF(Arguments::isSignatureValid(signature, Arguments::VariantSignature),
                     Error::MalformedMessageData);
        }
        // do not clobber nesting before potentially going to out_needMoreData!
        VALID_IF(d->m_nesting.beginVariant(), Error::MalformedMessageData);

        // use m_u as temporary storage - its contents are undefined anyway in state BeginVariant
        m_u.String.ptr = signature.ptr;
        m_u.String.length = signature.length;
        break; }

    case BeginArray: {
        // NB: Do not make non-idempotent changes to member variables before potentially going to
        //     out_needMoreData! We'll make the same change again after getting more data.
        uint32 arrayLength = 0;
        if (likely(!d->m_nilArrayNesting)) {
            if (unlikely(d->m_dataPosition + sizeof(uint32) > d->m_data.length)) {
                goto out_needMoreData;
            }
            arrayLength = basic::readUint32(d->m_data.ptr + d->m_dataPosition, d->m_args->d->m_isByteSwapped);
            VALID_IF(arrayLength <= SpecMaxArrayLength, Error::MalformedMessageData);
            d->m_dataPosition += sizeof(uint32);
        }

        const TypeInfo firstElementTy = typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]);
        m_state = firstElementTy.state() == BeginDict ? BeginDict : BeginArray;

        uint32 dataEnd = d->m_dataPosition;
        // In case (and we don't check this) the internal type has greater alignment requirements than the
        // array index type (which aligns to 4 bytes), align to the nonexistent first element.
        // d->m_nilArrayNesting is only increased when the API client calls beginArray(), so
        // d->m_nilArrayNesting is the old state. As a side effect of that, it is possible to implement the
        // requirement that, in nested containers inside empty arrays, only the outermost array's first type
        // is used for alignment purposes.
        // TODO: unit-test this
        if (likely(!d->m_nilArrayNesting)) {
            const uint32 padStart = d->m_dataPosition;
            d->m_dataPosition = align(d->m_dataPosition, firstElementTy.alignment);
            VALID_IF(isPaddingZero(d->m_data, padStart, d->m_dataPosition), Error::MalformedMessageData);
            dataEnd = d->m_dataPosition + arrayLength;
            if (unlikely(dataEnd > d->m_data.length)) {
                goto out_needMoreData;
            }
        }

        VALID_IF(d->m_nesting.beginArray(), Error::MalformedMessageData);
        if (firstElementTy.state() == BeginDict) {
            // TODO check whether the first type is a primitive or string type! // ### isn't that already
            // checked for the main signature and / or variants, though?
            // only closed at end of dict - there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen(), Error::MalformedMessageData);
        }
        // temporarily store the future ArrayInfo::dataEnd in m_u.Uint32. used by {begin,skip}{Array,Dict}()
        m_u.Uint32 = dataEnd;
        break; }

    default:
        assert(false);
        break;
    }

    return;

out_needMoreData:
    // we only start an array when the data for it has fully arrived (possible due to the length
    // prefix), so if we still run out of data in an array the input is invalid.
    VALID_IF(!d->m_nesting.array, Error::MalformedMessageData);
    m_state = NeedMoreData;
    d->m_signaturePosition = savedSignaturePosition;
    d->m_dataPosition = savedDataPosition;
}

void Arguments::Reader::skipArrayOrDictSignature(bool isDict)
{
    // Note that we cannot just pass a dummy Nesting instance to parseSingleCompleteType, it must
    // actually check the nesting because an array may contain other nested aggregates. So we must
    // compensate for the already raised nesting levels from BeginArray handling in advanceState().
    d->m_nesting.endArray();
    if (isDict) {
        d->m_nesting.endParen();
        // the Reader ad-hoc parsing code moved at ahead by one to skip the '{', but parseSingleCompleteType()
        // needs to see the full dict signature, so fix it up
        d->m_signaturePosition--;
    }

    // parse the full (i.e. starting with the 'a') array (or dict) signature in order to skip it -
    // barring bugs, must have been too deep nesting inside variants if parsing fails
    cstring remainingSig(d->m_signature.ptr + d->m_signaturePosition,
                         d->m_signature.length - d->m_signaturePosition);
    VALID_IF(parseSingleCompleteType(&remainingSig, &d->m_nesting), Error::MalformedMessageData);
    d->m_signaturePosition = d->m_signature.length - remainingSig.length;

    // Compensate for pre-increment in advanceState()
    d->m_signaturePosition--;

    d->m_nesting.beginArray();
    if (isDict) {
        d->m_nesting.beginParen();
        // Compensate for code in advanceState() that kind of ignores the '}' at the end of a dict.
        // Unlike advanceState(), parseSingleCompleteType() does properly parse that one.
        d->m_signaturePosition--;
    }
}

bool Arguments::Reader::beginArray(EmptyArrayOption option)
{
    if (unlikely(m_state != BeginArray)) {
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = BeginArray;
    Private::ArrayInfo &arrayInfo = aggregateInfo.arr; // also used for dict
    arrayInfo.dataEnd = m_u.Uint32;
    arrayInfo.containedTypeBegin = d->m_signaturePosition + 1;
    d->m_aggregateStack.push_back(aggregateInfo);

    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (!arrayLength) {
        d->m_nilArrayNesting++;
    }

    if (unlikely(d->m_nilArrayNesting && option == SkipIfEmpty)) {
        skipArrayOrDictSignature(false);
    }

    advanceState();
    return !d->m_nilArrayNesting;
}

void Arguments::Reader::skipArrayOrDict(bool isDict)
{
    // fast-forward the signature and data positions
    skipArrayOrDictSignature(isDict);
    d->m_dataPosition = m_u.Uint32;

    // m_state = isDict ? EndDict : EndArray; // nobody looks at it
    if (isDict) {
        d->m_nesting.endParen();
        d->m_signaturePosition++; // skip '}'
    }
    d->m_nesting.endArray();

    // proceed to next element
    advanceState();
}

void Arguments::Reader::skipArray()
{
    if (unlikely(m_state != BeginArray)) {
        // TODO test this
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipArrayOrDict(false);
    }
}

void Arguments::Reader::endArray()
{
    VALID_IF(m_state == EndArray, Error::ReadWrongType);
    d->m_signaturePosition--; // fix up for the pre-increment of d->m_signaturePosition in advanceState()
    d->m_nesting.endArray();
    d->m_aggregateStack.pop_back();
    if (unlikely(d->m_nilArrayNesting)) {
        d->m_nilArrayNesting--;
    }
    advanceState();
}

static bool isAligned(uint32 value, uint32 alignment)
{
    assert(alignment <= 8); // so zeroBits <= 3
    const uint32 zeroBits = alignmentLog2(alignment);
    return (value & (0x7u >> (3 - zeroBits))) == 0;
}

std::pair<Arguments::IoState, chunk> Arguments::Reader::readPrimitiveArray()
{
    auto ret = std::make_pair(InvalidData, chunk());

    if (m_state != BeginArray) {
        return ret;
    }

    // the point of "primitive array" accessors is that the data can be just memcpy()ed, so we
    // reject anything that needs validation, including booleans

    const TypeInfo elementType = typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]);
    if (!elementType.isPrimitive || elementType.state() == Boolean || elementType.state() == UnixFd) {
        return ret;
    }
    if (d->m_args->d->m_isByteSwapped && elementType.state() != Byte) {
        return ret;
    }

    const uint32 size = m_u.Uint32 - d->m_dataPosition;
    // does the end of data line up with the end of the last data element?
    if (!isAligned(size, elementType.alignment)) {
        return ret;
    }
    if (size) {
        ret.second.ptr = d->m_data.ptr + d->m_dataPosition;
        ret.second.length = size;
    }
    // No need to change  d->m_nilArrayNesting - it can't be observed while "in" the current array

    ret.first = elementType.state();
    d->m_signaturePosition += 1;
    d->m_dataPosition = m_u.Uint32;
    m_state = EndArray;
    d->m_nesting.endArray();

    // ... leave the array, there is nothing more to do in it
    advanceState();

    return ret;
}

Arguments::IoState Arguments::Reader::peekPrimitiveArray(EmptyArrayOption option) const
{
    // almost duplicated from readPrimitiveArray(), so keep it in sync
    if (m_state != BeginArray) {
        return InvalidData;
    }
    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (option == SkipIfEmpty && !arrayLength) {
        return BeginArray;
    }
    const TypeInfo elementType = typeInfo(d->m_signature.ptr[d->m_signaturePosition + 1]);
    if (!elementType.isPrimitive || elementType.state() == Boolean || elementType.state() == UnixFd) {
        return BeginArray;
    }
    if (d->m_args->d->m_isByteSwapped && elementType.state() != Byte) {
        return BeginArray;
    }
    return elementType.state();
}

bool Arguments::Reader::beginDict(EmptyArrayOption option)
{
    if (unlikely(m_state != BeginDict)) {
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
        return false;
    }

    d->m_signaturePosition++; // skip '{`

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = BeginDict;
    Private::ArrayInfo &arrayInfo = aggregateInfo.arr; // also used for dict
    arrayInfo.dataEnd = m_u.Uint32;
    arrayInfo.containedTypeBegin = d->m_signaturePosition + 1;
    d->m_aggregateStack.push_back(aggregateInfo);

    const uint32 arrayLength = m_u.Uint32 - d->m_dataPosition;
    if (!arrayLength) {
        d->m_nilArrayNesting++;
    }

    if (unlikely(d->m_nilArrayNesting && option == SkipIfEmpty)) {
        skipArrayOrDictSignature(true);
#ifdef WITH_DICT_ENTRY
        const bool ret = !d->m_nilArrayNesting;
        advanceState();
        endDictEntry();
        return ret;
    }
    m_state = BeginDictEntry;
#else
    }

    advanceState();
#endif
    return !d->m_nilArrayNesting;
}

void Arguments::Reader::skipDict()
{
    if (unlikely(m_state != BeginDict)) {
        // TODO test this
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        d->m_signaturePosition++; // skip '{' like beginDict() does - skipArrayOrDict() expects it
        skipArrayOrDict(true);
    }
}

bool Arguments::Reader::isDictKey() const
{
    if (!d->m_aggregateStack.empty()) {
        const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        return aggregateInfo.aggregateType == BeginDict &&
               d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin;
    }
    return false;
}

void Arguments::Reader::endDict()
{
    VALID_IF(m_state == EndDict, Error::ReadWrongType);
    d->m_nesting.endParen();
    //d->m_signaturePosition++; // skip '}'
    //d->m_signaturePosition--; // fix up for the pre-increment of d->m_signaturePosition in advanceState()
    d->m_nesting.endArray();
    d->m_aggregateStack.pop_back();
    if (unlikely(d->m_nilArrayNesting)) {
        d->m_nilArrayNesting--;
    }
    advanceState();
}

#ifdef WITH_DICT_ENTRY
void Arguments::Reader::beginDictEntry()
{
    VALID_IF(m_state == BeginDictEntry, Error::ReadWrongType);
    advanceState();
}

void Arguments::Reader::endDictEntry()
{
    VALID_IF(m_state == EndDictEntry, Error::ReadWrongType);
    if (m_u.Uint32 == 0) {
        m_state = BeginDictEntry;
    } else {
        m_state = EndDict;
    }
}
#endif

void Arguments::Reader::beginStruct()
{
    VALID_IF(m_state == BeginStruct, Error::ReadWrongType);
    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = BeginStruct;
    d->m_aggregateStack.push_back(aggregateInfo);
    advanceState();
}

void Arguments::Reader::skipStruct()
{
    if (unlikely(m_state != BeginStruct)) {
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipCurrentElement();
    }
}

void Arguments::Reader::endStruct()
{
    VALID_IF(m_state == EndStruct, Error::ReadWrongType);
    d->m_nesting.endParen();
    d->m_aggregateStack.pop_back();
    advanceState();
}

void Arguments::Reader::beginVariant()
{
    VALID_IF(m_state == BeginVariant, Error::ReadWrongType);

    Private::AggregateInfo aggregateInfo;
    aggregateInfo.aggregateType = BeginVariant;
    Private::VariantInfo &variantInfo = aggregateInfo.var;
    variantInfo.prevSignature.ptr = d->m_signature.ptr;
    variantInfo.prevSignature.length = d->m_signature.length;
    variantInfo.prevSignaturePosition = d->m_signaturePosition;
    d->m_aggregateStack.push_back(aggregateInfo);
    d->m_signature.ptr = m_u.String.ptr;
    d->m_signature.length = m_u.String.length;
    d->m_signaturePosition = uint32(-1); // we increment d->m_signaturePosition before reading a char

    advanceState();
}

void Arguments::Reader::skipVariant()
{
    if (unlikely(m_state != BeginVariant)) {
        m_state = InvalidData;
        d->m_error.setCode(Error::ReadWrongType);
    } else {
        skipCurrentElement();
    }
}

void Arguments::Reader::endVariant()
{
    VALID_IF(m_state == EndVariant, Error::ReadWrongType);
    d->m_nesting.endVariant();

    const Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
    const Private::VariantInfo &variantInfo = aggregateInfo.var;
    d->m_signature.ptr = variantInfo.prevSignature.ptr;
    d->m_signature.length = variantInfo.prevSignature.length;
    d->m_signaturePosition = variantInfo.prevSignaturePosition;
    d->m_aggregateStack.pop_back();

    advanceState();
}

void Arguments::Reader::skipCurrentElement()
{
    // ### We could implement a skipping fast path for more aggregates, but it would be a lot of work, so
    //     until it's proven to be a problem, just reuse what we have.

#ifndef NDEBUG
    Arguments::IoState stateOnEntry = m_state;
#endif
    int nestingLevel = 0;
    bool isDone = false;

    while (!isDone) {
        switch(state()) {
        case Arguments::Finished:
            // Okay, that's a bit weird. I guess the graceful way to handle it is do nothing in release
            // mode, and explode in debug mode in order to warn the API client.
            // (We could use a warning message facility here, make one?)
            assert(false);
            isDone = true;
            break;
        case Arguments::BeginStruct:
            beginStruct();
            nestingLevel++;
            break;
        case Arguments::EndStruct:
            endStruct();
            nestingLevel--;
            if (!nestingLevel) {
                assert(stateOnEntry == BeginStruct);
            }
            break;
        case Arguments::BeginVariant:
            beginVariant();
            nestingLevel++;
            break;
        case Arguments::EndVariant:
            endVariant();
            nestingLevel--;
            if (!nestingLevel) {
                assert(stateOnEntry == BeginVariant);
            }
            break;
        case Arguments::BeginArray:
            skipArray();
            break;
        case Arguments::EndArray:
            assert(stateOnEntry == EndArray); // only way this can happen - we gracefully skip EndArray
                                              // and DON'T decrease nestingLevel b/c it would go negative.
            endArray();
            break;
        case Arguments::BeginDict:
            skipDict();
            break;
#ifdef WITH_DICT_ENTRY
        case Arguments::BeginDictEntry:
            beginDictEntry();
            break;
        case Arguments::EndDictEntry:
            endDictEntry();
            break;
#endif
        case Arguments::EndDict:
            assert(stateOnEntry == EndDict); // only way this can happen - we gracefully "skip" EndDict
                                             // and DON'T decrease nestingLevel b/c it would go negative.
            endDict();
            break;
        case Arguments::Boolean:
            readBoolean();
            break;
        case Arguments::Byte:
            readByte();
            break;
        case Arguments::Int16:
            readInt16();
            break;
        case Arguments::Uint16:
            readUint16();
            break;
        case Arguments::Int32:
            readInt32();
            break;
        case Arguments::Uint32:
            readUint32();
            break;
        case Arguments::Int64:
            readInt64();
            break;
        case Arguments::Uint64:
            readUint64();
            break;
        case Arguments::Double:
            readDouble();
            break;
        case Arguments::String:
            readString();
            break;
        case Arguments::ObjectPath:
            readObjectPath();
            break;
        case Arguments::Signature:
            readSignature();
            break;
        case Arguments::UnixFd:
            readUnixFd();
            break;
        case Arguments::NeedMoreData:
            // TODO handle this properly: rewind the state to before the aggregate - or get fancy and support
            // resuming, but that is going to get really ugly
            // fall through
        default:
            m_state = InvalidData;
            d->m_error.setCode(Error::StateNotSkippable);
            // fall through
        case Arguments::InvalidData:
            isDone = true;
            break;
        }
        if (!nestingLevel) {
            isDone = true;
        }
    }
}

std::vector<Arguments::IoState> Arguments::Reader::aggregateStack() const
{
    std::vector<IoState> ret;
    ret.reserve(d->m_aggregateStack.size());
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        ret.push_back(aggregate.aggregateType);
    }
    return ret;
}

uint32 Arguments::Reader::aggregateDepth() const
{
    return d->m_aggregateStack.size();
}

Arguments::IoState Arguments::Reader::currentAggregate() const
{
    if (d->m_aggregateStack.empty()) {
        return NotStarted;
    }
    return d->m_aggregateStack.back().aggregateType;
}

Arguments::Writer::Private::Private(const Private &other)
{
    *this = other;
}

void Arguments::Writer::Private::operator=(const Private &other)
{
    if (&other == this) {
        assert(false); // if this happens, the (internal) caller did something wrong
        return;
    }

    m_dataElementsCountBeforeNilArray = other.m_dataElementsCountBeforeNilArray;
    m_dataPositionBeforeVariant = other.m_dataPositionBeforeVariant;

    m_nesting = other.m_nesting;
    m_signature.ptr = other.m_signature.ptr; // ### still needs adjustment, done after allocating m_data
    m_signature.length = other.m_signature.length;
    m_signaturePosition = other.m_signaturePosition;

    m_dataCapacity = other.m_dataCapacity;
    m_dataPosition = other.m_dataPosition;
    // handle *m_data and the data it's pointing to
    m_data = reinterpret_cast<byte *>(malloc(m_dataCapacity));
    memcpy(m_data, other.m_data, m_dataPosition);
    m_signature.ptr += m_data - other.m_data;

    m_nilArrayNesting = other.m_nilArrayNesting;
    m_fileDescriptors = other.m_fileDescriptors;
    m_error = other.m_error;

    m_aggregateStack = other.m_aggregateStack;
    m_queuedData = other.m_queuedData;
}

Arguments::Writer::Writer()
   : d(new(allocCaches.writerPrivate.allocate()) Private),
     m_state(AnyData)
{
}

Arguments::Writer::Writer(Writer &&other)
   : d(other.d),
     m_state(other.m_state),
     m_u(other.m_u)
{
    other.d = nullptr;
}

void Arguments::Writer::operator=(Writer &&other)
{
    if (&other == this) {
        return;
    }
    d = other.d;
    m_state = other.m_state;
    m_u = other.m_u;

    other.d = nullptr;
}

Arguments::Writer::Writer(const Writer &other)
   : d(nullptr),
     m_state(other.m_state),
     m_u(other.m_u)
{
    if (other.d) {
        d = new(allocCaches.writerPrivate.allocate()) Private(*other.d);
    }

}

void Arguments::Writer::operator=(const Writer &other)
{
    if (&other == this) {
        return;
    }
    m_state = other.m_state;
    m_u = other.m_u;
    if (d && other.d) {
        *d = *other.d;
    } else {
        Writer temp(other);
        std::swap(d, temp.d);
    }
}

Arguments::Writer::~Writer()
{
    free(d->m_data);
    d->m_data = nullptr;
    d->~Private();
    allocCaches.writerPrivate.free(d);
    d = nullptr;
}

bool Arguments::Writer::isValid() const
{
    return !d->m_error.isError();
}

Error Arguments::Writer::error() const
{
    return d->m_error;
}

cstring Arguments::Writer::stateString() const
{
    return printableState(m_state);
}

bool Arguments::Writer::isInsideEmptyArray() const
{
    return d->m_nilArrayNesting > 0;
}

cstring Arguments::Writer::currentSignature() const
{
    // ### see comment in Reader::currentSignature
    return cstring(d->m_signature.ptr,
                   std::max(uint32(0), std::min(d->m_signature.length, d->m_signaturePosition)));
}

void Arguments::Writer::doWritePrimitiveType(IoState type, uint32 alignAndSize)
{
    d->reserveData(d->m_dataPosition + (alignAndSize << 1));
    zeroPad(d->m_data, alignAndSize, &d->m_dataPosition);

    switch(type) {
    case Boolean: {
        uint32 num = m_u.Boolean ? 1 : 0;
        basic::writeUint32(d->m_data + d->m_dataPosition, num);
        break; }
    case Byte:
        d->m_data[d->m_dataPosition] = m_u.Byte;
        break;
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
        const uint32 index = d->m_fileDescriptors.size();
        if (!d->m_nilArrayNesting) {
            d->m_fileDescriptors.push_back(m_u.Int32);
        }
        basic::writeUint32(d->m_data + d->m_dataPosition, index);
        break; }
    default:
        assert(false);
        VALID_IF(false, Error::InvalidType);
    }

    d->m_dataPosition += alignAndSize;
    d->maybeQueueData(alignAndSize, alignAndSize);
}

void Arguments::Writer::doWriteString(IoState type, uint32 lengthPrefixSize)
{
    if (type == String) {
        VALID_IF(Arguments::isStringValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidString);
    } else if (type == ObjectPath) {
        VALID_IF(Arguments::isObjectPathValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidObjectPath);
    } else if (type == Signature) {
        VALID_IF(Arguments::isSignatureValid(cstring(m_u.String.ptr, m_u.String.length)),
                 Error::InvalidSignature);
    }

    d->reserveData(d->m_dataPosition + (lengthPrefixSize << 1) + m_u.String.length + 1);

    zeroPad(d->m_data, lengthPrefixSize, &d->m_dataPosition);

    if (lengthPrefixSize == 1) {
        d->m_data[d->m_dataPosition] = m_u.String.length;
    } else {
        basic::writeUint32(d->m_data + d->m_dataPosition, m_u.String.length);
    }
    d->m_dataPosition += lengthPrefixSize;
    d->maybeQueueData(lengthPrefixSize, lengthPrefixSize);

    d->appendBulkData(chunk(m_u.String.ptr, m_u.String.length + 1));
}

void Arguments::Writer::advanceState(cstring signatureFragment, IoState newState)
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

    m_state = AnyData;
    uint32 alignment = 1;
    bool isPrimitiveType = false;
    bool isStringType = false;

    if (signatureFragment.length) {
        const TypeInfo ty = typeInfo(signatureFragment.ptr[0]);
        alignment = ty.alignment;
        isPrimitiveType = ty.isPrimitive;
        isStringType = ty.isString;
    }

    bool isWritingSignature = d->m_signaturePosition == d->m_signature.length;
    if (isWritingSignature) {
        // signature additions must conform to syntax
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= MaxSignatureLength,
                 Error::SignatureTooLong);
    }
    if (!d->m_aggregateStack.empty()) {
        Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        switch (aggregateInfo.aggregateType) {
        case BeginVariant:
            // arrays and variants may contain just one single complete type; note that this will
            // trigger only when not inside an aggregate inside the variant or (see below) array
            if (d->m_signaturePosition >= 1) {
                VALID_IF(newState == EndVariant, Error::NotSingleCompleteTypeInVariant);
            }
            break;
        case BeginArray:
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1
                && newState != EndArray) {
                // we are not at start of contained type's signature, the array is at top of stack
                // -> we are at the end of the single complete type inside the array, start the next
                // entry. TODO: check compatibility (essentially what's in the else branch below)
                d->m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
                isWritingSignature = false;
            }
            break;
        case BeginDict:
            if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
#ifdef WITH_DICT_ENTRY
                if (aggregateInfo.arr.dictEntryState == Private::RequireBeginDictEntry) {
                    // This is only reached immediately after beginDict() so it's kinda wasteful, oh well.
                    VALID_IF(newState == BeginDictEntry, Error::MissingBeginDictEntry);
                    aggregateInfo.arr.dictEntryState = Private::InDictEntry;
                    m_state = DictKey;
                    return; // BeginDictEntry writes no data
                }
#endif
                VALID_IF(isPrimitiveType || isStringType, Error::InvalidKeyTypeInDict);
            }
#ifdef WITH_DICT_ENTRY
            // TODO test this part of the state machine
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2) {
                if (aggregateInfo.arr.dictEntryState == Private::RequireEndDictEntry) {
                    VALID_IF(newState == EndDictEntry, Error::MissingEndDictEntry);
                    aggregateInfo.arr.dictEntryState = Private::AfterEndDictEntry;
                    m_state = BeginDictEntry;
                    return; // EndDictEntry writes no data
                } else {
                    // v should've been caught earlier
                    assert(aggregateInfo.arr.dictEntryState == Private::AfterEndDictEntry);
                    VALID_IF(newState == BeginDictEntry || newState == EndDict, Error::MissingBeginDictEntry);
                    // "fall through", the rest (another iteration or finish) is handled below
                }
            } else if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 1) {
                assert(aggregateInfo.arr.dictEntryState == Private::InDictEntry);
                aggregateInfo.arr.dictEntryState = Private::RequireEndDictEntry;
                // Setting EndDictEntry after writing a primitive type works fine, but setting it after
                // ending another aggregate would be somewhat involved and need to happen somewhere
                // else, so just don't do that. We still produce an error when endDictEntry() is not
                // used correctly.
                // m_state = EndDictEntry;

                // continue and write the dict entry's value
            }
#endif
            // first type has been checked already, second must be present (checked in EndDict
            // state handler). no third type allowed.
            if (d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + 2
                && newState != EndDict) {
                // align to dict entry
                d->alignData(structAlignment);
                d->m_signaturePosition = aggregateInfo.arr.containedTypeBegin;
                isWritingSignature = false;
                m_state = DictKey;
#ifdef WITH_DICT_ENTRY
                assert(newState == BeginDictEntry);
                aggregateInfo.arr.dictEntryState = Private::InDictEntry;
                return; // BeginDictEntry writes no data
#endif
            }

            break;
        default:
            break;
        }
    }

    if (isWritingSignature) {
        // extend the signature
        for (uint32 i = 0; i < signatureFragment.length; i++) {
            d->m_signature.ptr[d->m_signaturePosition++] = signatureFragment.ptr[i];
        }
        d->m_signature.length += signatureFragment.length;
    } else {
        // Do not try to prevent several iterations through a nil array. Two reasons:
        // - We may be writing a nil array in the >1st iteration of a non-nil outer array.
        //   This would need to be distinguished from just iterating through a nil array
        //   several times. Which is well possible. We don't bother with that because...
        // - As a QtDBus unittest illustrates, somebody may choose to serialize a fixed length
        //   series of data elements as an array (instead of struct), so that a trivial
        //   serialization of such data just to fill in type information in an outer empty array
        //   would end up iterating through the inner, implicitly empty array several times.
        // All in all it is just not much of a benefit to be strict, so don't.
        //VALID_IF(likely(!d->m_nilArrayNesting), Error::ExtraIterationInEmptyArray);

        // signature must match first iteration (of an array/dict)
        VALID_IF(d->m_signaturePosition + signatureFragment.length <= d->m_signature.length,
                 Error::TypeMismatchInSubsequentArrayIteration);
        // TODO need to apply special checks for state changes with no explicit signature char?
        // (end of array, end of variant)
        for (uint32 i = 0; i < signatureFragment.length; i++) {
            VALID_IF(d->m_signature.ptr[d->m_signaturePosition++] == signatureFragment.ptr[i],
                     Error::TypeMismatchInSubsequentArrayIteration);
        }
    }

    if (isPrimitiveType) {
        doWritePrimitiveType(newState, alignment);
        return;
    }
    if (isStringType) {
        // In case of nil array, skip writing to make sure that the input string (which is explicitly
        // allowed to be garbage) is not validated and no wild pointer is dereferenced.
        if (likely(!d->m_nilArrayNesting)) {
            doWriteString(newState, alignment);
        } else {
            // The alignment of the first element in a nil array determines where array data starts,
            // which is needed to serialize the length correctly. Write the minimum to achieve that.
            // (The check to see if we're really at the first element is omitted - for performance
            // it's worth trying to add that check)
            d->alignData(alignment);
        }
        return;
    }

    Private::AggregateInfo aggregateInfo;

    switch (newState) {
    case BeginStruct:
        VALID_IF(d->m_nesting.beginParen(), Error::ExcessiveNesting);
        aggregateInfo.aggregateType = BeginStruct;
        aggregateInfo.sct.containedTypeBegin = d->m_signaturePosition;
        d->m_aggregateStack.push_back(aggregateInfo);
        d->alignData(alignment);
        break;
    case EndStruct:
        d->m_nesting.endParen();
        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndStructHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginStruct &&
                 d->m_signaturePosition > aggregateInfo.sct.containedTypeBegin + 1,
                 Error::EmptyStruct); // empty structs are not allowed
        d->m_aggregateStack.pop_back();
        break;

    case BeginVariant: {
        VALID_IF(d->m_nesting.beginVariant(), Error::ExcessiveNesting);
        aggregateInfo.aggregateType = BeginVariant;

        Private::VariantInfo &variantInfo = aggregateInfo.var;
        variantInfo.prevSignatureOffset = uint32(reinterpret_cast<byte *>(d->m_signature.ptr) - d->m_data);
        d->m_signature.ptr[-1] = byte(d->m_signature.length);
        variantInfo.prevSignaturePosition = d->m_signaturePosition;

        if (!d->insideVariant()) {
            d->m_dataPositionBeforeVariant = d->m_dataPosition;
        }

        d->m_aggregateStack.push_back(aggregateInfo);

        d->m_queuedData.reserve(16);
        d->m_queuedData.push_back(Private::QueuedDataInfo(1, Private::QueuedDataInfo::VariantSignature));

        const uint32 newDataPosition = d->m_dataPosition + Private::SignatureReservedSpace;
        d->reserveData(newDataPosition);
        // allocate new signature in the data buffer, reserve one byte for length prefix
        d->m_signature.ptr = reinterpret_cast<char *>(d->m_data) + d->m_dataPosition + 1;
        d->m_signature.length = 0;
        d->m_signaturePosition = 0;
        d->m_dataPosition = newDataPosition;
        break; }
    case EndVariant: {
        d->m_nesting.endVariant();
        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndVariantHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == BeginVariant, Error::CannotEndVariantHere);
        if (likely(!d->m_nilArrayNesting)) {
            // Empty variants are not allowed. As an exception, in nil arrays they are
            // allowed for writing a type signature like "av" in the shortest possible way.
            // No use adding stuff when it's not required or even possible.
            VALID_IF(d->m_signaturePosition > 0, Error::EmptyVariant);
            assert(d->m_signaturePosition <= MaxSignatureLength); // should have been caught earlier
        }
        d->m_signature.ptr[-1] = byte(d->m_signaturePosition);

        Private::VariantInfo &variantInfo = aggregateInfo.var;
        d->m_signature.ptr = reinterpret_cast<char *>(d->m_data) + variantInfo.prevSignatureOffset;
        d->m_signature.length = d->m_signature.ptr[-1];
        d->m_signaturePosition = variantInfo.prevSignaturePosition;
        d->m_aggregateStack.pop_back();

        // if not in any variant anymore, flush queued data and resume unqueued operation
        if (d->m_signature.ptr == reinterpret_cast<char *>(d->m_data) + 1) {
            flushQueuedData();
        }

        break; }

    case BeginDict:
    case BeginArray: {
        VALID_IF(d->m_nesting.beginArray(), Error::ExcessiveNesting);
        if (newState == BeginDict) {
            // not re-opened before each element: there is no observable difference for clients
            VALID_IF(d->m_nesting.beginParen(), Error::ExcessiveNesting);
        }

        aggregateInfo.aggregateType = newState;
        aggregateInfo.arr.containedTypeBegin = d->m_signaturePosition;

        d->reserveData(d->m_dataPosition + (sizeof(uint32) << 1));
        zeroPad(d->m_data, sizeof(uint32), &d->m_dataPosition);
        basic::writeUint32(d->m_data + d->m_dataPosition, 0);
        aggregateInfo.arr.lengthFieldPosition = d->m_dataPosition;
        d->m_dataPosition += sizeof(uint32);
        d->maybeQueueData(sizeof(uint32), Private::QueuedDataInfo::ArrayLengthField);

        if (newState == BeginDict) {
            d->alignData(structAlignment);
#ifdef WITH_DICT_ENTRY
            m_state = BeginDictEntry;
            aggregateInfo.arr.dictEntryState = Private::RequireBeginDictEntry;
#else
            m_state = DictKey;
#endif
        }

        d->m_aggregateStack.push_back(aggregateInfo);
        break; }
    case EndDict:
    case EndArray: {
        const bool isDict = newState == EndDict;
        if (isDict) {
            d->m_nesting.endParen();
        }
        d->m_nesting.endArray();
        VALID_IF(!d->m_aggregateStack.empty(), Error::CannotEndArrayHere);
        aggregateInfo = d->m_aggregateStack.back();
        VALID_IF(aggregateInfo.aggregateType == (isDict ? BeginDict : BeginArray),
                 Error::CannotEndArrayOrDictHere);
        VALID_IF(d->m_signaturePosition >= aggregateInfo.arr.containedTypeBegin + (isDict ? 3 : 1),
                 Error::TooFewTypesInArrayOrDict);

        // array data starts (and in empty arrays ends) at the first array element position *after alignment*
        const uint32 contentAlign = isDict ? 8
                        : typeInfo(d->m_signature.ptr[aggregateInfo.arr.containedTypeBegin]).alignment;
        const uint32 arrayDataStart = align(aggregateInfo.arr.lengthFieldPosition + sizeof(uint32),
                                            contentAlign);

        if (unlikely(d->m_nilArrayNesting)) {
            if (--d->m_nilArrayNesting == 0) {
                d->m_dataPosition = arrayDataStart;
                if (d->insideVariant()) {
                    assert(d->m_queuedData.begin() + d->m_dataElementsCountBeforeNilArray <=
                           d->m_queuedData.end());
                    d->m_queuedData.erase(d->m_queuedData.begin() + d->m_dataElementsCountBeforeNilArray,
                                          d->m_queuedData.end());
                    assert((d->m_queuedData.end() - 2)->size == Private::QueuedDataInfo::ArrayLengthField);
                    // align, but don't have actual data for the first element
                    d->m_queuedData.back().size = 0;
                }
            }
        }

        // (arrange to) patch in the array length now that it is known
        if (d->insideVariant()) {
            d->m_queuedData.push_back(Private::QueuedDataInfo(1, Private::QueuedDataInfo::ArrayLengthEndMark));
        } else {
            basic::writeUint32(d->m_data + aggregateInfo.arr.lengthFieldPosition,
                               d->m_dataPosition - arrayDataStart);
        }
        d->m_aggregateStack.pop_back();
        break; }
#ifdef WITH_DICT_ENTRY
    case BeginDictEntry:
    case EndDictEntry:
        break;
#endif
    default:
        VALID_IF(false, Error::InvalidType);
        break;
    }
}

void Arguments::Writer::beginArrayOrDict(IoState beginWhat, ArrayOption option)
{
    assert(beginWhat == BeginArray || beginWhat == BeginDict);
    if (unlikely(option == RestartEmptyArrayToWriteTypes)) {
        if (!d->m_aggregateStack.empty()) {
            Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
            if (aggregateInfo.aggregateType == beginWhat) {
                // No writes to the array or dict may have occurred yet

                if (d->m_signaturePosition == aggregateInfo.arr.containedTypeBegin) {
                    // Fix up state as if beginArray/Dict() had been called with WriteTypesOfEmptyArray
                    // in the first place. After that small fixup we're done and return.
                    // The code is a slightly modified version of code below under: if (isEmpty) {
                    if (!d->m_nilArrayNesting) {
                        d->m_nilArrayNesting = 1;
                        d->m_dataElementsCountBeforeNilArray = d->m_queuedData.size() + 2; // +2 as below
                        // Now correct for the elements already added in advanceState() with BeginArray / BeginDict
                        d->m_dataElementsCountBeforeNilArray -= (beginWhat == BeginDict) ? 2 : 1;
                    } else {
                        // The array may be implicitly nil (so our poor API client doesn't notice) because
                        // an array below in the aggregate stack is nil, so just allow this as a no-op.
                    }
                    return;
                }
            }
        }
        VALID_IF(false, Error::InvalidStateToRestartEmptyArray);
    }

    const bool isEmpty = (option != NonEmptyArray) || d->m_nilArrayNesting;
    if (isEmpty) {
        if (!d->m_nilArrayNesting++) {
            // For simplictiy and performance in the fast path, we keep storing the data chunks and any
            // variant signatures written inside an empty array. When we close the array, though, we
            // throw away all that data and signatures and keep only changes in the signature containing
            // the topmost empty array.
            // +2 -> keep ArrayLengthField, and first data element for alignment purposes
            d->m_dataElementsCountBeforeNilArray = d->m_queuedData.size() + 2;
        }
    }
    if (beginWhat == BeginArray) {
        advanceState(cstring("a", strlen("a")), beginWhat);
    } else {
        advanceState(cstring("a{", strlen("a{")), beginWhat);
    }
}

void Arguments::Writer::beginArray(ArrayOption option)
{
    beginArrayOrDict(BeginArray, option);
}

void Arguments::Writer::endArray()
{
    advanceState(cstring(), EndArray);
}

void Arguments::Writer::beginDict(ArrayOption option)
{
    beginArrayOrDict(BeginDict, option);
}

void Arguments::Writer::endDict()
{
    advanceState(cstring("}", strlen("}")), EndDict);
}

#ifdef WITH_DICT_ENTRY
void Arguments::Writer::beginDictEntry()
{
    VALID_IF(m_state == BeginDictEntry, Error::MisplacedBeginDictEntry);
    advanceState(cstring(), BeginDictEntry);
}

void Arguments::Writer::endDictEntry()
{
    if (!d->m_aggregateStack.empty()) {
        Private::AggregateInfo &aggregateInfo = d->m_aggregateStack.back();
        if (aggregateInfo.aggregateType == BeginDict
            && aggregateInfo.arr.dictEntryState == Private::RequireEndDictEntry) {
            advanceState(cstring(), EndDictEntry);
            return;
        }
    }
    VALID_IF(false, Error::MisplacedEndDictEntry);
}
#endif

void Arguments::Writer::beginStruct()
{
    advanceState(cstring("(", strlen("(")), BeginStruct);
}

void Arguments::Writer::endStruct()
{
    advanceState(cstring(")", strlen(")")), EndStruct);
}

void Arguments::Writer::beginVariant()
{
    advanceState(cstring("v", strlen("v")), BeginVariant);
}

void Arguments::Writer::endVariant()
{
    advanceState(cstring(), EndVariant);
}

void Arguments::Writer::writeVariantForMessageHeader(char sig)
{
    // Note: the sugnature we're vorking with there is a(yv)
    // If we know that and can trust the client, this can be very easy and fast...
    d->m_signature.ptr[3] = 'v';
    d->m_signature.length = 4;
    d->m_signaturePosition = 4;

    d->reserveData(d->m_dataPosition + 3);
    d->m_data[d->m_dataPosition++] = 1;
    d->m_data[d->m_dataPosition++] = sig;
    d->m_data[d->m_dataPosition++] = 0;
}

void Arguments::Writer::fixupAfterWriteVariantForMessageHeader()
{
    // We just wrote something to the main signature when we shouldn't have.
    d->m_signature.length = 4;
    d->m_signaturePosition = 4;
}

void Arguments::Writer::writePrimitiveArray(IoState type, chunk data)
{
    const char letterCode = letterForPrimitiveIoState(type);
    if (letterCode == 'c' || data.length > SpecMaxArrayLength) {
        m_state = InvalidData;
        d->m_error.setCode(Error::NotPrimitiveType);
        return;
    }

    const TypeInfo elementType = typeInfo(letterCode);

    if (!isAligned(data.length, elementType.alignment)) {
        return;
    }

    beginArray(data.length ? NonEmptyArray : WriteTypesOfEmptyArray);

    // dummy write to write the signature...
    m_u.Uint64 = 0;
    advanceState(cstring(&letterCode, /*length*/ 1), elementType.state());

    if (!data.length) {
        // oh! a nil array.
        endArray();
        return;
    }

    // undo the dummy write (except for the preceding alignment bytes, if any)
    d->m_dataPosition -= elementType.alignment;
    if (d->insideVariant()) {
        d->m_queuedData.pop_back();
        d->m_queuedData.push_back(Private::QueuedDataInfo(elementType.alignment, 0));
    }

    // append the payload
    d->reserveData(d->m_dataPosition + data.length);
    d->appendBulkData(data);

    endArray();
}

Arguments Arguments::Writer::finish()
{
    // what needs to happen here:
    // - check if the message can be closed - basically the aggregate stack must be empty
    // - close the signature by adding the terminating null
    // TODO set error in returned Arguments in error cases

    Arguments args;

    if (m_state == InvalidData) {
        return args;
    }
    if (d->m_nesting.total() != 0) {
        m_state = InvalidData;
        d->m_error.setCode(Error::CannotEndArgumentsHere);
        return args;
    }
    assert(!d->m_nilArrayNesting);
    assert(!d->insideVariant());

    assert(d->m_signaturePosition <= MaxSignatureLength); // this should have been caught before
    assert(d->m_signature.ptr == reinterpret_cast<char *>(d->m_data) + 1);

    // Note that we still keep the full SignatureReservedSpace for the main signature, which means
    // less copying around to shrink the gap between signature and data, but also wastes an enormous
    // amount of space (relative to the possible minimum) in some cases. It should not be a big space
    // problem because normally not many D-Bus Message / Arguments instances exist at the same time.

    d->m_signature.length = d->m_signaturePosition;
    d->m_signature.ptr[d->m_signature.length] = '\0';
    args.d->m_error = d->m_error;

    // OK, so this length check is more of a sanity check. The actual limit limits the size of the
    // full message. Here we take the size of the "payload" and don't add the size of the signature -
    // why bother doing it accurately when the real check with full information comes later anyway?
    bool success = true;
    const uint32 dataSize = d->m_dataPosition - Private::SignatureReservedSpace;
    if (success && dataSize > SpecMaxMessageLength) {
        success = false;
        d->m_error.setCode(Error::ArgumentsTooLong);
    }

    if (!dataSize || !success) {
        args.d->m_memOwnership = nullptr;
        args.d->m_signature = cstring();
        args.d->m_data = chunk();
    } else {
        args.d->m_memOwnership = d->m_data;
        args.d->m_signature = cstring(d->m_data + 1 /* w/o length prefix */, d->m_signature.length);
        args.d->m_data = chunk(d->m_data + Private::SignatureReservedSpace, dataSize);
        d->m_data = nullptr; // now owned by Arguments and later freed there
    }

    if (!success) {
        m_state = InvalidData;
        return Arguments();
    }
    args.d->m_fileDescriptors = std::move(d->m_fileDescriptors);
    m_state = Finished;
    return std::move(args);
}

struct ArrayLengthField
{
    uint32 lengthFieldPosition;
    uint32 dataStartPosition;
};

void Arguments::Writer::flushQueuedData()
{
    const uint32 count = d->m_queuedData.size();
    assert(count); // just don't call this method otherwise!

    // Note: if one of signature or data is nonempty, the other must also be nonempty.
    // Even "empty" things like empty arrays or null strings have a size field, in that case
    // (for all(?) types) of value zero.

    // Copy the signature and main data (thus the whole contents) into one allocated block,
    // which is good to have for performance and simplicity reasons.

    // The maximum alignment blowup for naturally aligned types is just less than a factor of 2.
    // Structs and dict entries are always 8 byte aligned so they add a maximum blowup of 7 bytes
    // each (when they contain a byte).
    // Those estimates are very conservative (but easy!), so some space optimization is possible.

    uint32 inPos = d->m_dataPositionBeforeVariant;
    uint32 outPos = d->m_dataPositionBeforeVariant;
    byte *const buffer = d->m_data;

    std::vector<ArrayLengthField> lengthFieldStack;

    for (uint32 i = 0; i < count; i++) {
        const Private::QueuedDataInfo ei = d->m_queuedData[i];
        switch (ei.size) {
        case 0: {
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
            }
            break;
        default: {
                assert(ei.size && ei.size <= Private::QueuedDataInfo::LargestSize);
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
                // copy data chunk
                memmove(buffer + outPos, buffer + inPos, ei.size);
                inPos += ei.size;
                outPos += ei.size;
            }
            break;
        case Private::QueuedDataInfo::ArrayLengthField: {
                //   start of an array
                // alignment padding before length field
                inPos = align(inPos, ei.alignment());
                zeroPad(buffer, ei.alignment(), &outPos);
                // reserve length field
                ArrayLengthField al;
                al.lengthFieldPosition = outPos;
                inPos += sizeof(uint32);
                outPos += sizeof(uint32);
                // alignment padding before first array element
                assert(i + 1 < d->m_queuedData.size());
                const uint32 contentsAlignment = d->m_queuedData[i + 1].alignment();
                inPos = align(inPos, contentsAlignment);
                zeroPad(buffer, contentsAlignment, &outPos);
                // array data starts at the first array element position after alignment
                al.dataStartPosition = outPos;
                lengthFieldStack.push_back(al);
            }
            break;
        case Private::QueuedDataInfo::ArrayLengthEndMark: {
                //   end of an array
                // just put the now known array length in front of the array
                const ArrayLengthField al = lengthFieldStack.back();
                const uint32 arrayLength = outPos - al.dataStartPosition;
                if (arrayLength > SpecMaxArrayLength) {
                    m_state = InvalidData;
                    d->m_error.setCode(Error::ArrayOrDictTooLong);
                    i = count + 1; // break out of the loop
                    break;
                }
                basic::writeUint32(buffer + al.lengthFieldPosition, arrayLength);
                lengthFieldStack.pop_back();
            }
            break;
        case Private::QueuedDataInfo::VariantSignature: {
                // move the signature and add its null terminator
                const uint32 length = buffer[inPos] + 1; // + length prefix
                memmove(buffer + outPos, buffer + inPos, length);
                buffer[outPos + length] = '\0';
                outPos += length + 1; // + null terminator
                inPos += Private::Private::SignatureReservedSpace;
            }
            break;
        }
    }
    assert(m_state == InvalidData || lengthFieldStack.empty());

    d->m_dataPosition = outPos;
    d->m_queuedData.clear();
}

std::vector<Arguments::IoState> Arguments::Writer::aggregateStack() const
{
    std::vector<IoState> ret;
    ret.reserve(d->m_aggregateStack.size());
    for (Private::AggregateInfo &aggregate : d->m_aggregateStack) {
        ret.push_back(aggregate.aggregateType);
    }
    return ret;
}

uint32 Arguments::Writer::aggregateDepth() const
{
    return d->m_aggregateStack.size();
}

Arguments::IoState Arguments::Writer::currentAggregate() const
{
    if (d->m_aggregateStack.empty()) {
        return NotStarted;
    }
    return d->m_aggregateStack.back().aggregateType;
}

void Arguments::Writer::writeBoolean(bool b)
{
    m_u.Boolean = b;
    advanceState(cstring("b", strlen("b")), Boolean);
}

void Arguments::Writer::writeByte(byte b)
{
    m_u.Byte = b;
    advanceState(cstring("y", strlen("y")), Byte);
}

void Arguments::Writer::writeInt16(int16 i)
{
    m_u.Int16 = i;
    advanceState(cstring("n", strlen("n")), Int16);
}

void Arguments::Writer::writeUint16(uint16 i)
{
    m_u.Uint16 = i;
    advanceState(cstring("q", strlen("q")), Uint16);
}

void Arguments::Writer::writeInt32(int32 i)
{
    m_u.Int32 = i;
    advanceState(cstring("i", strlen("i")), Int32);
}

void Arguments::Writer::writeUint32(uint32 i)
{
    m_u.Uint32 = i;
    advanceState(cstring("u", strlen("u")), Uint32);
}

void Arguments::Writer::writeInt64(int64 i)
{
    m_u.Int64 = i;
    advanceState(cstring("x", strlen("x")), Int64);
}

void Arguments::Writer::writeUint64(uint64 i)
{
    m_u.Uint64 = i;
    advanceState(cstring("t", strlen("t")), Uint64);
}

void Arguments::Writer::writeDouble(double d)
{
    m_u.Double = d;
    advanceState(cstring("d", strlen("d")), Double);
}

void Arguments::Writer::writeString(cstring string)
{
    m_u.String.ptr = string.ptr;
    m_u.String.length = string.length;
    advanceState(cstring("s", strlen("s")), String);
}

void Arguments::Writer::writeObjectPath(cstring objectPath)
{
    m_u.String.ptr = objectPath.ptr;
    m_u.String.length = objectPath.length;
    advanceState(cstring("o", strlen("o")), ObjectPath);
}

void Arguments::Writer::writeSignature(cstring signature)
{
    m_u.String.ptr = signature.ptr;
    m_u.String.length = signature.length;
    advanceState(cstring("g", strlen("g")), Signature);
}

void Arguments::Writer::writeUnixFd(int32 fd)
{
    m_u.Int32 = fd;
    advanceState(cstring("h", strlen("h")), UnixFd);
}
