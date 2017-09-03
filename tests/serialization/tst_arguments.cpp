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

#include "../testutil.h"

#include <algorithm>
#include <cstring>
#include <iostream>

// Handy helpers

static void printChunk(chunk a)
{
    std::cout << "Array: ";
    for (uint32 i = 0; i < a.length; i++) {
        std::cout << int(a.ptr[i]) << '|';
    }
    std::cout << '\n';
}

static bool chunksEqual(chunk a1, chunk a2)
{
    if (a1.length != a2.length) {
        std::cout << "Different lengths.\n";
        printChunk(a1);
        printChunk(a2);
        return false;
    }
    for (uint32 i = 0; i < a1.length; i++) {
        if (a1.ptr[i] != a2.ptr[i]) {
            std::cout << "Different content.\n";
            printChunk(a1);
            printChunk(a2);
            return false;
        }
    }
    return true;
}

static bool stringsEqual(cstring s1, cstring s2)
{
    return chunksEqual(chunk(s1.ptr, s1.length), chunk(s2.ptr, s2.length));
}

static void maybeBeginDictEntry(Arguments::Writer *writer)
{
    (void) writer;
#ifdef WITH_DICT_ENTRY
    writer->beginDictEntry();
#endif
}

static void maybeEndDictEntry(Arguments::Writer *writer)
{
    (void) writer;
#ifdef WITH_DICT_ENTRY
    writer->endDictEntry();
#endif
}

// This class does:
// 1) iterates over the full Arguments with m_reader
// 2) skips whole aggregates at and below nesting level m_skipAggregatesFromLevel with m_skippingReader
// 3) skips nil arrays at and below nil array nesting level m_skipNilArraysFromLevel with m_skippingReader
// It even skips aggregates inside nil arrays as 2) + 3) imply.
// It checks:
// a) where nothing is skipped, that the aggregate structure and data read are the same
class SkipChecker
{
public:
    SkipChecker(Arguments::Reader *reader, Arguments::Reader *skippingReader,
                int skipAggregatesFromLevel, int skipNilArraysFromLevel)
       : m_nestingLevel(0),
         m_nilArrayNesting(0),
         m_skipAggregatesFromLevel(skipAggregatesFromLevel),
         m_skipNilArraysFromLevel(skipNilArraysFromLevel),
         m_reader(reader),
         m_skippingReader(skippingReader)
    {}

    template<typename F>
    void readAndCompare(F readFunc)
    {
        Arguments::IoState rState = m_reader->state();
        auto rval = (*m_reader.*readFunc)();
        if (m_nestingLevel < m_skipAggregatesFromLevel && m_nilArrayNesting < m_skipNilArraysFromLevel) {
            Arguments::IoState sState = m_skippingReader->state();
            TEST(rState == sState);
            auto sval = (*m_skippingReader.*readFunc)();
            if (!m_nilArrayNesting) {
                TEST(myEqual(rval, sval));
            }
        }
    }
#ifdef WITH_DICT_ENTRY
    void beginDictEntry()
    {
        m_reader->beginDictEntry();
        if (m_nestingLevel < m_skipAggregatesFromLevel && m_nilArrayNesting < m_skipNilArraysFromLevel) {
            m_skippingReader->beginDictEntry();
        }
    }

    void endDictEntry()
    {
        m_reader->endDictEntry();
        if (m_nestingLevel < m_skipAggregatesFromLevel && m_nilArrayNesting < m_skipNilArraysFromLevel) {
            m_skippingReader->endDictEntry();
        }

    }
#endif
    template<typename F, typename G>
    void beginAggregate(F beginFunc, G skipFunc)
    {
        (*m_reader.*beginFunc)();
        m_nestingLevel++;

        if (m_nilArrayNesting < m_skipNilArraysFromLevel) {
            if (m_nestingLevel < m_skipAggregatesFromLevel) {
                (*m_skippingReader.*beginFunc)();
            } else if (m_nestingLevel == m_skipAggregatesFromLevel) {
                (*m_skippingReader.*skipFunc)();
            }
        }
    }

    template<typename F, typename G>
    void beginArrayAggregate(F beginFunc, G skipFunc)
    {
        const bool hasData = (*m_reader.*beginFunc)(Arguments::Reader::ReadTypesOnlyIfEmpty);
        m_nestingLevel++;
        m_nilArrayNesting += hasData ? 0 : 1;

        if (m_nestingLevel > m_skipAggregatesFromLevel || m_nilArrayNesting > m_skipNilArraysFromLevel) {
            // we're already skipping, do nothing
        } else if (m_nestingLevel == m_skipAggregatesFromLevel) {
            (*m_skippingReader.*skipFunc)();
        } else if (m_nilArrayNesting == m_skipNilArraysFromLevel) {
            (*m_skippingReader.*beginFunc)(Arguments::Reader::SkipIfEmpty);
        } else {
            (*m_skippingReader.*beginFunc)(Arguments::Reader::ReadTypesOnlyIfEmpty);
        }
    }

    template<typename F>
    void endAggregate(F endFunc, bool isArrayType)
    {
        (*m_reader.*endFunc)();

        // when skipping a nil array: do the last part of the beginArray(), endArray() sequence
        // when using skip*(): do not call end() on that level, skip*() moves right past the aggregate
        if (m_nestingLevel < m_skipAggregatesFromLevel &&
            (m_nilArrayNesting < m_skipNilArraysFromLevel ||
             (isArrayType && m_nilArrayNesting == m_skipNilArraysFromLevel))) {
            (*m_skippingReader.*endFunc)();
        } else {
            // we've already skipped the current aggregate
        }

        m_nestingLevel--;
        if (isArrayType && m_nilArrayNesting) {
            m_nilArrayNesting--;
        }
    }

    int m_nestingLevel;
    int m_nilArrayNesting;
    const int m_skipAggregatesFromLevel;
    const int m_skipNilArraysFromLevel;

private:
    template<typename T> bool myEqual(const T &a, const T &b) { return a == b; }
    bool myEqua(const chunk &a, const chunk &b) { return chunksEqual(a, b); }
    bool myEqual(const cstring &a, const cstring &b) { return stringsEqual(a, b); }

    Arguments::Reader *m_reader;
    Arguments::Reader *m_skippingReader;
};

static void testReadWithSkip(const Arguments &arg, bool debugPrint)
{
    // it would be even better to decide when to skip more "randomly", but given that it doesn't make
    // much difference in the implementation, this should do.
    // loop over when to skip aggregates voluntarily (on "skipper")
    for (int aggregateSkipLevel = 1 /* 1 orig, 15 = H4X disabled*/; aggregateSkipLevel < 16;
         aggregateSkipLevel++) {
        // loop over when to skip empty aka nil arrays  - on "reader", which:
        // - cross checks aggregate skipping vs. skipping nil arrays
        // - is also the primary test for nil arrays
        for (int nilArraySkipLevel = 1; nilArraySkipLevel < 8; nilArraySkipLevel++) {
            // loop over *how* to skip empty aka nil arrays,
            // beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty) or skipArray()

            Arguments::Reader reader(arg);
            Arguments::Reader skippingReader(arg);
            SkipChecker checker(&reader, &skippingReader, aggregateSkipLevel, nilArraySkipLevel);

            bool isDone = false;

            while (!isDone) {
                TEST(reader.state() != Arguments::InvalidData);
                TEST(skippingReader.state() != Arguments::InvalidData);

                if (debugPrint) {
                    std::cerr << "Reader state: " << reader.stateString().ptr << '\n';
                    std::cerr << "Skipping reader state: " << skippingReader.stateString().ptr << '\n';
                }

                switch(reader.state()) {
                case Arguments::Finished:
                    TEST(checker.m_nestingLevel == 0);
                    TEST(checker.m_nilArrayNesting == 0);
                    isDone = true;
                    break;
                case Arguments::BeginStruct:
                    //std::cerr << "Beginning struct\n";
                    checker.beginAggregate(&Arguments::Reader::beginStruct, &Arguments::Reader::skipStruct);
                    break;
                case Arguments::EndStruct:
                    checker.endAggregate(&Arguments::Reader::endStruct, false);
                    break;
                case Arguments::BeginVariant:
                    //std::cerr << "Beginning variant\n";
                    checker.beginAggregate(&Arguments::Reader::beginVariant, &Arguments::Reader::skipVariant);
                    break;
                case Arguments::EndVariant:
                    checker.endAggregate(&Arguments::Reader::endVariant, false);
                    break;
                case Arguments::BeginArray:
                    checker.beginArrayAggregate(&Arguments::Reader::beginArray, &Arguments::Reader::skipArray);
                    break;
                case Arguments::EndArray:
                    checker.endAggregate(&Arguments::Reader::endArray, true);
                    break;
                case Arguments::BeginDict:
                    checker.beginArrayAggregate(&Arguments::Reader::beginDict, &Arguments::Reader::skipDict);
                    break;
#ifdef WITH_DICT_ENTRY
                case Arguments::BeginDictEntry:
                    checker.beginDictEntry();
                    break;
                case Arguments::EndDictEntry:
                    checker.endDictEntry();
                    break;
#endif
                case Arguments::EndDict:
                    checker.endAggregate(&Arguments::Reader::endDict, true);
                    break;
                case Arguments::Byte:
                    checker.readAndCompare(&Arguments::Reader::readByte);
                    break;
                case Arguments::Boolean:
                    checker.readAndCompare(&Arguments::Reader::readBoolean);
                    break;
                case Arguments::Int16:
                    checker.readAndCompare(&Arguments::Reader::readInt16);
                    break;
                case Arguments::Uint16:
                    checker.readAndCompare(&Arguments::Reader::readUint16);
                    break;
                case Arguments::Int32:
                    checker.readAndCompare(&Arguments::Reader::readInt32);
                    break;
                case Arguments::Uint32:
                    checker.readAndCompare(&Arguments::Reader::readUint32);
                    break;
                case Arguments::Int64:
                    checker.readAndCompare(&Arguments::Reader::readInt64);
                    break;
                case Arguments::Uint64:
                    checker.readAndCompare(&Arguments::Reader::readUint64);
                    break;
                case Arguments::Double:
                    checker.readAndCompare(&Arguments::Reader::readDouble);
                    break;
                case Arguments::String:
                    checker.readAndCompare(&Arguments::Reader::readString);
                    break;
                case Arguments::ObjectPath:
                    checker.readAndCompare(&Arguments::Reader::readObjectPath);
                    break;
                case Arguments::Signature:
                    checker.readAndCompare(&Arguments::Reader::readSignature);
                    break;
                case Arguments::UnixFd:
                    checker.readAndCompare(&Arguments::Reader::readUnixFd);
                    break;

                case Arguments::NeedMoreData:
                    // ### would be nice to test this as well
                default:
                    TEST(false);
                    break;
                }
            }

            TEST(reader.state() == Arguments::Finished);
            TEST(skippingReader.state() == Arguments::Finished);
        }
    }
}

// When using this to iterate over the reader, it will make an exact copy using the Writer.
// You need to do something only in states where something special should happen.
static void defaultReadToWrite(Arguments::Reader *reader, Arguments::Writer *writer)
{
    switch(reader->state()) {
    case Arguments::BeginStruct:
    case Arguments::EndStruct:
    case Arguments::BeginVariant:
    case Arguments::EndVariant:
    case Arguments::EndArray:
#ifdef WITH_DICT_ENTRY
    case Arguments::BeginDictEntry:
    case Arguments::EndDictEntry:
#endif
    case Arguments::EndDict:
    case Arguments::Byte:
    case Arguments::Boolean:
    case Arguments::Int16:
    case Arguments::Uint16:
    case Arguments::Int32:
    case Arguments::Uint32:
    case Arguments::Int64:
    case Arguments::Uint64:
    case Arguments::Double:
    case Arguments::UnixFd:
        Arguments::copyOneElement(reader, writer);
        break;
    // special handling for BeginArray and BeginDict to avoid "fast copy" for primitive arrays
    case Arguments::BeginArray: {
        const bool hasData = reader->beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
        writer->beginArray(hasData ? Arguments::Writer::NonEmptyArray
                                    : Arguments::Writer::WriteTypesOfEmptyArray);
        break; }
    case Arguments::BeginDict: {
        const bool hasData = reader->beginDict(Arguments::Reader::ReadTypesOnlyIfEmpty);
        writer->beginDict(hasData ? Arguments::Writer::NonEmptyArray
                                    : Arguments::Writer::WriteTypesOfEmptyArray);
        break; }
    case Arguments::String: {
        const cstring s = reader->readString();
        if (!reader->isInsideEmptyArray()) {
            TEST(Arguments::isStringValid(s));
        }
        writer->writeString(s);
        break; }
    case Arguments::ObjectPath: {
        const cstring objectPath = reader->readObjectPath();
        if (!reader->isInsideEmptyArray()) {
            TEST(Arguments::isObjectPathValid(objectPath));
        }
        writer->writeObjectPath(objectPath);
        break; }
    case Arguments::Signature: {
        const cstring signature = reader->readSignature();
        if (!reader->isInsideEmptyArray()) {
            TEST(Arguments::isSignatureValid(signature));
        }
        writer->writeSignature(signature);
        break; }

        writer->writeUnixFd(reader->readUnixFd());
        break;
    // special cases follow
    case Arguments::Finished:
        break; // You *probably* want to handle that one in the caller, but you don't have to
    case Arguments::NeedMoreData:
        TEST(false); // No way to handle that one here
        break;
    default:
        TEST(false);
        break;
    }
}

static void verifyAfterRoundtrip(const Arguments &original, const Arguments::Reader &originalReader,
                                 const Arguments &copy, const Arguments::Writer &copyWriter,
                                 bool debugPrint)
{
    TEST(originalReader.state() == Arguments::Finished);
    TEST(copyWriter.state() == Arguments::Finished);
    cstring originalSignature = original.signature();
    cstring copySignature = copy.signature();
    if (originalSignature.length) {
        TEST(Arguments::isSignatureValid(copySignature));
        TEST(stringsEqual(originalSignature, copySignature));
    } else {
        TEST(copySignature.length == 0);
    }

    chunk originalData = original.data();

    chunk copyData = copy.data();
    TEST(originalData.length == copyData.length);
    if (debugPrint && !chunksEqual(originalData, copyData)) {
        printChunk(originalData);
        printChunk(copyData);
    }
    TEST(chunksEqual(originalData, copyData));
}

static void doRoundtripWithShortReads(const Arguments &original, uint32 dataIncrement, bool debugPrint)
{
    const chunk data = original.data();
    chunk shortData;

    Arguments arg(nullptr, original.signature(), shortData, original.fileDescriptors());
    Arguments::Reader reader(arg);
    Arguments::Writer writer;

    bool isDone = false;

    while (!isDone) {
        TEST(writer.state() != Arguments::InvalidData);
        if (debugPrint) {
            std::cout << "Reader state: " << reader.stateString().ptr << '\n';
        }

        switch(reader.state()) {
        case Arguments::Finished:
            isDone = true;
            break;
        case Arguments::NeedMoreData: {
            TEST(shortData.length < data.length);
            // reallocate shortData to test that Reader can handle the data moving around - and
            // allocate the new one before destroying the old one to make sure that the pointer differs
            chunk oldData = shortData;
            shortData.length = std::min(shortData.length + dataIncrement, data.length);
            shortData.ptr = reinterpret_cast<byte *>(malloc(shortData.length));
            for (uint32 i = 0; i < shortData.length; i++) {
                shortData.ptr[i] = data.ptr[i];
            }
            // clobber it to provoke errors that only valgrind might find otherwise
            for (uint32 i = 0; i < oldData.length; i++) {
                oldData.ptr[i] = 0xff;
            }
            if (oldData.ptr) {
                free(oldData.ptr);
            }
            reader.replaceData(shortData);
            break; }
        default:
            defaultReadToWrite(&reader, &writer);
            break;
        }
    }

    Arguments copy = writer.finish();
    verifyAfterRoundtrip(original, reader, copy, writer, debugPrint);
    if (shortData.ptr) {
        free(shortData.ptr);
    }
}

static void doRoundtripWithReaderCopy(const Arguments &original, uint32 dataIncrement, bool debugPrint)
{
    Arguments::Reader *reader = new Arguments::Reader(original);
    Arguments::Writer writer;

    bool isDone = false;
    uint32 i = 0;

    while (!isDone) {
        TEST(writer.state() != Arguments::InvalidData);
        if (i++ == dataIncrement) {
            Arguments::Reader *copy = new Arguments::Reader(*reader);
            delete reader;
            reader = copy;
        }
        if (debugPrint) {
            std::cout << "Reader state: " << reader->stateString().ptr << '\n';
        }
        switch(reader->state()) {
        case Arguments::Finished:
            isDone = true;
            break;
        default:
            defaultReadToWrite(reader, &writer);
            break;
        }
    }

    Arguments copy = writer.finish();
    verifyAfterRoundtrip(original, *reader, copy, writer, debugPrint);
    delete reader;
}

static void doRoundtripWithWriterCopy(const Arguments &original, uint32 dataIncrement, bool debugPrint)
{
    Arguments::Reader reader(original);
    Arguments::Writer *writer = new Arguments::Writer;

    bool isDone = false;
    uint32 i = 0;

    while (!isDone) {
        TEST(writer->state() != Arguments::InvalidData);
        if (i++ == dataIncrement) {
            Arguments::Writer *copy = new Arguments::Writer(*writer);
            delete writer;
            writer = copy;
        }
        if (debugPrint) {
            std::cout << "Reader state: " << reader.stateString().ptr << '\n';
        }
        switch(reader.state()) {
        case Arguments::Finished:
            isDone = true;
            break;
        default:
            defaultReadToWrite(&reader, writer);
            break;
        }
    }

    Arguments copy = writer->finish();
    verifyAfterRoundtrip(original, reader, copy, *writer, debugPrint);
    delete writer;
}

static void doRoundtripForReal(const Arguments &original, uint32 dataIncrement, bool debugPrint)
{
    doRoundtripWithShortReads(original, dataIncrement, debugPrint);
    doRoundtripWithReaderCopy(original, dataIncrement, debugPrint);
    doRoundtripWithWriterCopy(original, dataIncrement, debugPrint);
}

// not returning by value to avoid the move constructor or assignment operator -
// those should have separate tests
static Arguments *shallowCopy(const Arguments &original)
{
    // File descriptors can't do shallow copies - don't care for now, file descriptors are an identity
    // type, not a value type (and therefore don't fit well into the whole data model), and in the vast
    // majority of messages there aren't any.
    cstring signature = original.signature();
    chunk data = original.data();
    return new Arguments(nullptr, signature, data, original.fileDescriptors());
}

static void shallowAssign(Arguments *copy, const Arguments &original)
{
    cstring signature = original.signature();
    chunk data = original.data();
    *copy = Arguments(nullptr, signature, data, original.fileDescriptors());
}

static void doRoundtripWithCopyAssignEtc(const Arguments &arg_in, uint32 dataIncrement, bool debugPrint)
{
    {
        // just pass through
        doRoundtripForReal(arg_in, dataIncrement, debugPrint);
    }
    {
        // shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        doRoundtripForReal(*shallowDuplicate, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // assignment from shallow copy
        Arguments shallowAssigned;
        shallowAssign(&shallowAssigned, arg_in);
        doRoundtripForReal(shallowAssigned, dataIncrement, debugPrint);
    }
    {
        // deep copy
        Arguments original(arg_in);
        doRoundtripForReal(original, dataIncrement, debugPrint);
    }
    {
        // move construction from shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        Arguments shallowMoveConstructed(std::move(*shallowDuplicate));
        doRoundtripForReal(shallowMoveConstructed, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // move assignment (hopefully, may the compiler optimize this to move-construction?) from shallow copy
        Arguments *shallowDuplicate = shallowCopy(arg_in);
        Arguments shallowMoveAssigned;
        shallowMoveAssigned = std::move(*shallowDuplicate);
        doRoundtripForReal(shallowMoveAssigned, dataIncrement, debugPrint);
        delete shallowDuplicate;
    }
    {
        // move construction from deep copy
        Arguments duplicate(arg_in);
        Arguments moveConstructed(std::move(duplicate));
        doRoundtripForReal(moveConstructed, dataIncrement, debugPrint);
    }
    {
        // move assignment (hopefully, may the compiler optimize this to move-construction?) from deep copy
        Arguments duplicate(arg_in);
        Arguments moveAssigned;
        moveAssigned = std::move(duplicate);
        doRoundtripForReal(moveAssigned, dataIncrement, debugPrint);
    }
}

static void doRoundtrip(const Arguments &arg, bool debugPrint = false)
{
    const uint32 maxIncrement = arg.data().length;
    for (uint32 i = 1; i <= maxIncrement; i++) {
        doRoundtripWithCopyAssignEtc(arg, i, debugPrint);
    }

    testReadWithSkip(arg, debugPrint);
}



// Tests proper



static void test_stringValidation()
{
    {
        cstring emptyWithNull("");
        cstring emptyWithoutNull;

        TEST(!Arguments::isStringValid(emptyWithoutNull));
        TEST(Arguments::isStringValid(emptyWithNull));

        TEST(!Arguments::isObjectPathValid(emptyWithoutNull));
        TEST(!Arguments::isObjectPathValid(emptyWithNull));

        TEST(Arguments::isSignatureValid(emptyWithNull));
        TEST(!Arguments::isSignatureValid(emptyWithoutNull));
        TEST(!Arguments::isSignatureValid(emptyWithNull, Arguments::VariantSignature));
        TEST(!Arguments::isSignatureValid(emptyWithoutNull, Arguments::VariantSignature));
    }
    {
        cstring trivial("i");
        TEST(Arguments::isSignatureValid(trivial));
        TEST(Arguments::isSignatureValid(trivial, Arguments::VariantSignature));
    }
    {
        cstring list("iqb");
        TEST(Arguments::isSignatureValid(list));
        TEST(!Arguments::isSignatureValid(list, Arguments::VariantSignature));
        cstring list2("aii");
        TEST(Arguments::isSignatureValid(list2));
        TEST(!Arguments::isSignatureValid(list2, Arguments::VariantSignature));
    }
    {
        cstring simpleArray("ai");
        TEST(Arguments::isSignatureValid(simpleArray));
        TEST(Arguments::isSignatureValid(simpleArray, Arguments::VariantSignature));
    }
    {
        cstring messyArray("a(iaia{ia{iv}})");
        TEST(Arguments::isSignatureValid(messyArray));
        TEST(Arguments::isSignatureValid(messyArray, Arguments::VariantSignature));
    }
    {
        cstring dictFail("a{vi}");
        TEST(!Arguments::isSignatureValid(dictFail));
        TEST(!Arguments::isSignatureValid(dictFail, Arguments::VariantSignature));
    }
    {
        cstring emptyStruct("()");
        TEST(!Arguments::isSignatureValid(emptyStruct));
        TEST(!Arguments::isSignatureValid(emptyStruct, Arguments::VariantSignature));
        cstring emptyStruct2("(())");
        TEST(!Arguments::isSignatureValid(emptyStruct2));
        TEST(!Arguments::isSignatureValid(emptyStruct2, Arguments::VariantSignature));
        cstring miniStruct("(t)");
        TEST(Arguments::isSignatureValid(miniStruct));
        TEST(Arguments::isSignatureValid(miniStruct, Arguments::VariantSignature));
        cstring badStruct("((i)");
        TEST(!Arguments::isSignatureValid(badStruct));
        TEST(!Arguments::isSignatureValid(badStruct, Arguments::VariantSignature));
        cstring badStruct2("(i))");
        TEST(!Arguments::isSignatureValid(badStruct2));
        TEST(!Arguments::isSignatureValid(badStruct2, Arguments::VariantSignature));
    }
    {
        cstring nullStr;
        cstring emptyStr("");
        TEST(!Arguments::isObjectPathValid(nullStr));
        TEST(!Arguments::isObjectPathValid(emptyStr));
        TEST(Arguments::isObjectPathValid(cstring("/")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc/")));
        TEST(Arguments::isObjectPathValid(cstring("/abc")));
        TEST(Arguments::isObjectPathValid(cstring("/abc/def")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc&def")));
        TEST(!Arguments::isObjectPathValid(cstring("/abc//def")));
        TEST(Arguments::isObjectPathValid(cstring("/aZ/0123_zAZa9_/_")));
    }
    {
        cstring maxStruct("((((((((((((((((((((((((((((((((i"
                          "))))))))))))))))))))))))))))))))");
        TEST(Arguments::isSignatureValid(maxStruct));
        TEST(Arguments::isSignatureValid(maxStruct, Arguments::VariantSignature));
        cstring struct33("(((((((((((((((((((((((((((((((((i" // too much nesting by one
                         ")))))))))))))))))))))))))))))))))");
        TEST(!Arguments::isSignatureValid(struct33));
        TEST(!Arguments::isSignatureValid(struct33, Arguments::VariantSignature));

        cstring maxArray("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(Arguments::isSignatureValid(maxArray));
        TEST(Arguments::isSignatureValid(maxArray, Arguments::VariantSignature));
        cstring array33("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(!Arguments::isSignatureValid(array33));
        TEST(!Arguments::isSignatureValid(array33, Arguments::VariantSignature));
    }
}

static void test_nesting()
{
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginArray();
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginArray();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginDict();
            maybeBeginDictEntry(&writer);
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginStruct();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 32; i++) {
            writer.beginDict();
            maybeBeginDictEntry(&writer);
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginArray();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        for (int i = 0; i < 64; i++) {
            writer.beginVariant();
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.beginVariant();
        TEST(writer.state() == Arguments::InvalidData);
    }
}

struct LengthPrefixedData
{
    uint32 length;
    byte data[256];
};

static void test_roundtrip()
{
    doRoundtrip(Arguments(nullptr, cstring(""), chunk()));
    {
        byte data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        doRoundtrip(Arguments(nullptr, cstring("i"), chunk(data, 4)));
        doRoundtrip(Arguments(nullptr, cstring("yyyy"), chunk(data, 4)));
        doRoundtrip(Arguments(nullptr, cstring("iy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("iiy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("nquy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("unqy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("nqy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("qny"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("yyny"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("qyyy"), chunk(data, 5)));
        doRoundtrip(Arguments(nullptr, cstring("d"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("dy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("x"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("xy"), chunk(data, 9)));
        doRoundtrip(Arguments(nullptr, cstring("t"), chunk(data, 8)));
        doRoundtrip(Arguments(nullptr, cstring("ty"), chunk(data, 9)));
    }
    {
        LengthPrefixedData testArray = {0, {0}};
        for (int i = 0; i < 64; i++) {
            testArray.data[i] = i;
        }
        byte *testData = reinterpret_cast<byte *>(&testArray);

        testArray.length = 1;
        doRoundtrip(Arguments(nullptr, cstring("ay"), chunk(testData, 5)));
        testArray.length = 4;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 8)));
        testArray.length = 8;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 12)));
        testArray.length = 64;
        doRoundtrip(Arguments(nullptr, cstring("ai"), chunk(testData, 68)));
        doRoundtrip(Arguments(nullptr, cstring("an"), chunk(testData, 68)));

        testArray.data[0] = 0; testArray.data[1] = 0; // zero out padding
        testArray.data[2] = 0; testArray.data[3] = 0;
        testArray.length = 56;
        doRoundtrip(Arguments(nullptr, cstring("ad"), chunk(testData, 64)));
    }
    {
        LengthPrefixedData testString;
        for (int i = 0; i < 200; i++) {
            testString.data[i] = 'A' + i % 53; // stay in the 7-bit ASCII range
        }
        testString.data[200] = '\0';
        testString.length = 200;
        byte *testData = reinterpret_cast<byte *>(&testString);
        doRoundtrip(Arguments(nullptr, cstring("s"), chunk(testData, 205)));
    }
    {
        LengthPrefixedData testDict;
        testDict.length = 2;
        testDict.data[0] = 0; testDict.data[1] = 0; // zero padding; dict entries are always 8-aligned.
        testDict.data[2] = 0; testDict.data[3] = 0;

        testDict.data[4] = 23;
        testDict.data[5] = 42;
        byte *testData = reinterpret_cast<byte *>(&testDict);
        doRoundtrip(Arguments(nullptr, cstring("a{yy}"), chunk(testData, 10)));
    }
    {
        byte testData[36] = {
            5, // variant signature length
            '(', 'y', 'g', 'd', ')', '\0', // signature: struct of: byte, signature (easiest because
                                           //   its length prefix is byte order independent), double
            0,      // pad to 8-byte boundary for struct
            23,     // the byte
            6, 'i', 'a', '{', 'i', 'v', '}', '\0', // the signature
            0, 0, 0, 0, 0, 0, 0,    // padding to 24 bytes (next 8-byte boundary)
            1, 2, 3, 4, 5, 6, 7, 8, // the double
            20, 21, 22, 23 // the int (not part of the variant)
        };
        doRoundtrip(Arguments(nullptr, cstring("vi"), chunk(testData, sizeof(testData))));
    }
    {
        // Spec says: alignment padding after array length, even if the array contains no data. Test this
        // with different types and alignment situations.
        byte testData[40] = {
            0, 0, 0, 0, // length of array of uint64s - zero
            0, 0, 0, 0, // alignment padding to 8 bytes (= natural alignment of uint64)
            // ... zero uint64s ...
            1, 2, 3, 4, // a uint32 to change the alignment, just to test
            0, 0, 0, 0, // length of array of int64s - zero
            // no alignment padding needed here
            0, 0, 0, 0, // length of dict {uint32, uint32} - zero
            0, 0, 0, 0, // alignment padding to 8 bytes (= alignment of dict entry)
            // some data (single bytes) between the arrays to prevent all those zeros from accidentally
            // looking valid when the Reader is confused. Also upset the alignment a bit.
            101, 102, 103, 104, 105,
            0, 0, 0, // padding to alignment of array size
            0, 0, 0, 0, // length of array of structs - zero
            0, 0, 0, 0  // alignment padding to 8 bytes (= alignment of struct)
        };
        doRoundtrip(Arguments(nullptr, cstring("atuaxa{uu}yyyyya(u)"), chunk(testData, sizeof(testData))));
    }
}

static void test_writerMisuse()
{
    // Array
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.endArray(); // wrong,  must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.endArray(); // even with no elements it, must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.writeByte(1);
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.endArray(); // wrong, must contain exactly one type
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.writeByte(1);
        writer.writeUint16(2);  // wrong, different from first element
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginVariant();
        writer.endVariant(); // empty variants are okay if and only if inside an empty array
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
    }
    // Dict
    {
        Arguments::Writer writer;
        writer.beginDict();
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict();
        writer.writeByte(1);
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(1);
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict();
        maybeBeginDictEntry(&writer);
        writer.writeByte(1);
        writer.writeByte(2);
        maybeEndDictEntry(&writer);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict();
        maybeBeginDictEntry(&writer);
        writer.writeByte(1);
        writer.writeByte(2);
        maybeEndDictEntry(&writer);
        // second key-value pair
        maybeBeginDictEntry(&writer);
        TEST(writer.state() != Arguments::InvalidData);
        writer.writeUint16(3); // wrong, incompatible with first element
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginDict();
        maybeBeginDictEntry(&writer);
        writer.writeByte(1);
        writer.writeByte(2);
        maybeEndDictEntry(&writer);
        // second key-value pair
        maybeBeginDictEntry(&writer);
        writer.writeByte(3);
        TEST(writer.state() != Arguments::InvalidData);
        writer.writeUint16(4); // wrong, incompatible with first element
        TEST(writer.state() == Arguments::InvalidData);
    }

    {
        Arguments::Writer writer;
        writer.beginDict();
        maybeBeginDictEntry(&writer);
        writer.beginVariant(); // wrong, key type must be basic
        TEST(writer.state() == Arguments::InvalidData);
    }
    // Variant
    {
        // this and the next are a baseline to make sure that the following test fails for a good reason
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeByte(1);
        writer.endVariant();
        TEST(writer.state() != Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.endVariant();
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeByte(1);
        writer.writeByte(2); // wrong, a variant may contain only one or zero single complete types
        TEST(writer.state() == Arguments::InvalidData);
    }
    {
        Arguments::Writer writer;
        writer.beginStruct();
        writer.writeByte(1);
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::InvalidData); // can't finish while inside an aggregate
        TEST(arg.signature().length == 0); // should not be written on error
    }
}

static void addSomeVariantStuff(Arguments::Writer *writer)
{
    // maybe should have typed the following into hackertyper.com to make it look more "legit" ;)
    static const char *aVeryLongString = "ujfgosuideuvcevfgeoauiyetoraedtmzaubeodtraueonuljfgonuiljofnuilojf"
                                         "0ij948h534ownlyejglunh4owny9hw3v9woni09ulgh4wuvc<l9foehujfigosuij"
                                         "ofgnua0j3409k0ae9nyatrnoadgiaeh0j98hejuohslijolsojiaeojaufhesoujh";
    writer->beginVariant();
        writer->beginVariant();
            writer->beginVariant();
                writer->beginStruct();
                    writer->writeString(cstring("Smoerebroed smoerebroed"));
                    writer->beginStruct();
                        writer->writeString(cstring(aVeryLongString));
                        writer->writeString(cstring("Bork bork bork"));
                        writer->beginVariant();
                            writer->beginStruct();
                                writer->writeString(cstring("Quite nesty"));
                                writer->writeObjectPath(cstring("/path/to/object"));
                                writer->writeUint64(234234234);
                                writer->writeByte(2);
                                writer->writeUint64(234234223434);
                                writer->writeUint16(34);
                            writer->endStruct();
                        writer->endVariant();
                        writer->beginStruct();
                            writer->writeByte(34);
                        writer->endStruct();
                    writer->endStruct();
                    writer->writeString(cstring("Another string"));
                writer->endStruct();
            writer->endVariant();
        writer->endVariant();
    writer->endVariant();
}

static void test_complicated()
{
    Arguments arg;
    {
        Arguments::Writer writer;
        // NeedMoreData-related bugs are less dangerous inside arrays, so we try to provoke one here;
        // the reason for arrays preventing failures is that they have a length prefix which enables
        // and encourages pre-fetching all the array's data before processing *anything* inside the
        // array. therefore no NeedMoreData state happens while really deserializing the array's
        // contents. but we exactly want NeedMoreData while in the middle of deserializing something
        // meaty, specifically variants. see Reader::replaceData().
        addSomeVariantStuff(&writer);

        writer.writeInt64(234234);
        writer.writeByte(115);
        writer.beginVariant();
            writer.beginDict();
                maybeBeginDictEntry(&writer);
                writer.writeByte(23);
                writer.beginVariant();
                    writer.writeString(cstring("twenty-three"));
                writer.endVariant();
                maybeEndDictEntry(&writer);
                // key-value pair 2
                maybeBeginDictEntry(&writer);
                writer.writeByte(83);
                writer.beginVariant();
                writer.writeObjectPath(cstring("/foo/bar/object"));
                writer.endVariant();
                maybeEndDictEntry(&writer);
                // key-value pair 3
                maybeBeginDictEntry(&writer);
                writer.writeByte(234);
                writer.beginVariant();
                    writer.beginArray();
                        writer.writeUint16(234);
                        writer.writeUint16(234);
                        writer.writeUint16(234);
                    writer.endArray();
                writer.endVariant();
                maybeEndDictEntry(&writer);
                // key-value pair 4
                maybeBeginDictEntry(&writer);
                writer.writeByte(25);
                writer.beginVariant();
                    addSomeVariantStuff(&writer);
                writer.endVariant();
                maybeEndDictEntry(&writer);
            writer.endDict();
        writer.endVariant();
        writer.writeString("Hello D-Bus!");
        writer.beginArray();
            writer.writeDouble(1.567898);
            writer.writeDouble(1.523428);
            writer.writeDouble(1.621133);
            writer.writeDouble(1.982342);
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        arg = writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
    }
    doRoundtrip(arg);
}

static void test_alignment()
{
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray();
        writer.writeByte(64);
        writer.endArray();
        for (int i = 123; i < 150; i++) {
            writer.writeByte(i);
        }

        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginStruct();
        writer.writeByte(110);
        writer.endStruct();
        writer.writeByte(200);
        Arguments arg = writer.finish();
        doRoundtrip(arg);
    }
}

static void test_arrayOfVariant()
{
    // non-empty array
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray();
        writer.beginVariant();
        writer.writeByte(64);
        writer.endVariant();
        writer.endArray();
        writer.writeByte(123);

        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
    // empty array
    {
        Arguments::Writer writer;
        writer.writeByte(123);
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginVariant();
        writer.endVariant();
        writer.endArray();
        writer.writeByte(123);

        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
        doRoundtrip(arg);
    }
}

static void test_realMessage()
{
    Arguments arg;
    // non-empty array
    {
        Arguments::Writer writer;

        writer.writeString(cstring("message"));
        writer.writeString(cstring("konversation"));

        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginVariant();
        writer.endVariant();
        writer.endArray();

        writer.writeString(cstring(""));
        writer.writeString(cstring("&lt;fredrikh&gt; he's never on irc"));

        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(123); // may not show up in the output
        writer.endArray();

        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeString(cstring("dummy, I may not show up in the output!"));
        writer.endArray();

        writer.writeInt32(-1);
        writer.writeInt64(46137372);

        TEST(writer.state() != Arguments::InvalidData);
        arg = writer.finish();
        TEST(writer.state() != Arguments::InvalidData);
    }
    doRoundtrip(arg);
}

static void test_isWritingSignatureBug()
{
    {
        // This was the original test, so it's the one with the comments :)
        Arguments::Writer writer;
        writer.beginArray();
            writer.beginStruct();
                writer.beginDict();
                    maybeBeginDictEntry(&writer);
                    writer.writeByte(1);
                    writer.writeByte(2);
                    maybeEndDictEntry(&writer);
                writer.endDict();
                // Must add more stuff after the inner dict to ensure that the signature position of the
                // dict's value is well inside the existing signature in the second dict entry.
                // See isWritingSignature in Writer::advanceState().
                writer.writeUint16(1);
                writer.writeUint16(2);
            writer.endStruct();
            writer.beginStruct();
                writer.beginDict();
                    maybeBeginDictEntry(&writer);
                    writer.writeByte(1);
                    writer.writeByte(2);
                    maybeEndDictEntry(&writer);
                    // In the second pass, we are definitely NOT writing a new part of the dict signature,
                    // which used to go (that was the bug!!) through a different code path in
                    // Arguments::Writer::advanceState().
                    maybeBeginDictEntry(&writer);
                    writer.writeByte(1);
                    TEST(writer.state() != Arguments::InvalidData);
                    writer.writeUint16(2);
                    TEST(writer.state() == Arguments::InvalidData);
    }
    {
        // For completeness, do the equivalent of the previous test with an array inside
        Arguments::Writer writer;
        writer.beginArray();
            writer.beginStruct();
                writer.beginArray();
                    writer.writeByte(1);
                writer.endArray();
                writer.writeUint16(1);
                writer.writeUint16(2);
            writer.endStruct();
            writer.beginStruct();
                writer.beginArray();
                    writer.writeByte(1);
                    writer.writeByte(1);
                    TEST(writer.state() != Arguments::InvalidData);
                    writer.writeUint16(2);
                    TEST(writer.state() == Arguments::InvalidData);
    }
}

static void writeValue(Arguments::Writer *writer, uint32 typeIndex, const void *value)
{
    switch (typeIndex) {
    case 0:
        break;
    case 1:
        writer->writeByte(*static_cast<const byte *>(value)); break;
    case 2:
        writer->writeUint16(*static_cast<const uint16 *>(value)); break;
    case 3:
        writer->writeUint32(*static_cast<const uint32 *>(value)); break;
    case 4:
        writer->writeUint64(*static_cast<const uint64 *>(value)); break;
    default:
        TEST(false);
    }
}

static bool checkValue(Arguments::Reader *reader, uint32 typeIndex, const void *expected)
{
    switch (typeIndex) {
    case 0:
        return true;
    case 1:
        return reader->readByte() == *static_cast<const byte *>(expected);
    case 2:
        return reader->readUint16() == *static_cast<const uint16 *>(expected);
    case 3:
        return reader->readUint32() == *static_cast<const uint32 *>(expected);
    case 4:
        return reader->readUint64() == *static_cast<const uint64 *>(expected);
    default:
        TEST(false);
    }
    return false;
}

static void test_primitiveArray()
{
    // TODO also test some error cases

    static const uint32 testDataSize = 16384;
    byte testData[testDataSize];
    for (uint32 i = 0; i < testDataSize; i++) {
        testData[i] = i & 0xff;
    }

    for (uint i = 0; i < 4; i++) {

        const bool writeAsPrimitive = i & 0x1;
        const bool readAsPrimitive = i & 0x2;

        static const uint32 arrayTypesCount = 5;
        // those types must be compatible with writeValue() and readValue()
        static Arguments::IoState arrayTypes[arrayTypesCount] = {
            Arguments::InvalidData,
            Arguments::Byte,
            Arguments::Uint16,
            Arguments::Uint32,
            Arguments::Uint64
        };

        for (uint otherType = 0; otherType < arrayTypesCount; otherType++) {

            // an array with no type in it is ill-formed, so we start with 1 (Byte)
            for (uint typeInArray = 1; typeInArray < arrayTypesCount; typeInArray++) {

                static const uint32 arraySizesCount = 12;
                static const uint32 arraySizes[arraySizesCount] = {
                    0,
                    1,
                    2,
                    3,
                    4,
                    7,
                    8,
                    9,
                    511,
                    512,
                    513,
                    2048 // dataSize / sizeof(uint64) == 2048
                };

                for (uint k = 0; k < arraySizesCount; k++) {

                    static const uint64_t otherValue = ~0llu;
                    const uint32 arraySize = arraySizes[k];
                    const uint32 dataSize = arraySize << (typeInArray - 1);
                    TEST(dataSize <= testDataSize);

                    Arguments arg;
                    {
                        Arguments::Writer writer;

                        // write something before the array to test different starting position alignments
                        writeValue(&writer, otherType, &otherValue);

                        if (writeAsPrimitive) {
                            writer.writePrimitiveArray(arrayTypes[typeInArray], chunk(testData, dataSize));
                        } else {
                            writer.beginArray(arraySize ? Arguments::Writer::NonEmptyArray
                                                        : Arguments::Writer::WriteTypesOfEmptyArray);
                            byte *testDataPtr = testData;
                            if (arraySize) {
                                for (uint m = 0; m < arraySize; m++) {
                                    writeValue(&writer, typeInArray, testDataPtr);
                                    testDataPtr += 1 << (typeInArray - 1);
                                }
                            } else {
                                writeValue(&writer, typeInArray, testDataPtr);
                            }
                            writer.endArray();
                        }

                        TEST(writer.state() != Arguments::InvalidData);
                        // TEST(writer.state() == Arguments::AnyData);
                        // TODO do we handle AnyData consistently, and do we really need it anyway?
                        writeValue(&writer, otherType, &otherValue);
                        TEST(writer.state() != Arguments::InvalidData);
                        arg = writer.finish();
                        TEST(writer.state() == Arguments::Finished);
                    }

                    {
                        Arguments::Reader reader(arg);

                        TEST(checkValue(&reader, otherType, &otherValue));

                        if (readAsPrimitive) {
                            TEST(reader.state() == Arguments::BeginArray);
                            std::pair<Arguments::IoState, chunk> ret = reader.readPrimitiveArray();
                            TEST(ret.first == arrayTypes[typeInArray]);
                            TEST(chunksEqual(chunk(testData, dataSize), ret.second));
                        } else {
                            TEST(reader.state() == Arguments::BeginArray);
                            const bool hasData = reader.beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
                            TEST(hasData == (arraySize != 0));
                            TEST(reader.state() != Arguments::InvalidData);
                            byte *testDataPtr = testData;

                            if (arraySize) {
                                for (uint m = 0; m < arraySize; m++) {
                                    TEST(reader.state() != Arguments::InvalidData);
                                    TEST(checkValue(&reader, typeInArray, testDataPtr));
                                    TEST(reader.state() != Arguments::InvalidData);
                                    testDataPtr += 1 << (typeInArray - 1);
                                }
                            } else {
                                TEST(reader.state() == arrayTypes[typeInArray]);
                                // next: dummy read, necessary to move forward; value is ignored
                                checkValue(&reader, typeInArray, testDataPtr);
                                TEST(reader.state() != Arguments::InvalidData);
                            }

                            TEST(reader.state() == Arguments::EndArray);
                            reader.endArray();
                            TEST(reader.state() != Arguments::InvalidData);
                        }

                        TEST(reader.state() != Arguments::InvalidData);
                        TEST(checkValue(&reader, otherType, &otherValue));
                        TEST(reader.state() == Arguments::Finished);
                    }

                    // the data generated here nicely stresses the empty array skipping code
                    if (i == 0 && arraySize < 100) {
                        testReadWithSkip(arg, false);
                    }
                }
            }
        }
    }
}

static void test_signatureLengths()
{
    for (int i = 0; i <= 256; i++) {
        Arguments::Writer writer;
        for (int j = 0; j < i; j++) {
            writer.writeByte(255);
        }
        if (i == 256) {
            TEST(writer.state() == Arguments::InvalidData);
            break;
        }
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);

        // The full doRoundtrip() just here makes this whole file take several seconds to execute
        // instead of a fraction of a second. This way is much quicker.
        doRoundtripForReal(arg, 2048, false);
        Arguments argCopy = arg;
        doRoundtripForReal(argCopy, 2048, false);
    }
    for (int i = 1 /* variants may not be empty */; i <= 256; i++) {
        Arguments::Writer writer;

        writer.beginVariant();
        switch (i) {
        case 0:
            TEST(false);
        case 1:
            writer.writeByte(255);
            break;
        case 2:
            // "ay" signature is two letters
            writer.beginArray();
            writer.writeByte(255);
            writer.endArray();
            break;
        default:
            // (y), (yy), ...
            writer.beginStruct();
            for (int j = strlen("()"); j < i; j++) {
                writer.writeByte(255);
            }
            writer.endStruct();
            break;
        }
        writer.endVariant();

        if (i == 256) {
            TEST(writer.state() == Arguments::InvalidData);
            break;
        }
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);

        doRoundtripForReal(arg, 2048, false);
        Arguments argCopy = arg;
        doRoundtripForReal(argCopy, 2048, false);
    }
}

static void test_emptyArrayAndDict()
{
    // Arrays
    {
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(0);
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(0);
        writer.endArray();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginStruct();
        writer.writeByte(0);
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(0);
        writer.endArray();
        writer.endStruct();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.writeUint32(987654321);
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginStruct();
        writer.writeDouble(0);
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeByte(0);
        writer.endArray();
        writer.endStruct();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.writeString(cstring("xy"));
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginStruct();
        writer.writeUint32(12345678);
        //It is implicitly clear that an array inside a nil array is also nil
        //writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        //TODO add a test for writing >1 element in nested empty array - I've tried that and it fails
        //     like it should, but it needs a proper standalone test
        writer.beginArray();
        writer.writeByte(0);
        writer.endArray();
        writer.writeByte(12);
        writer.endStruct();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.writeString(cstring("xy"));
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.beginStruct();
        writer.writeByte(123);
        writer.beginVariant();
        writer.endVariant();
        writer.endStruct();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        for (int i = 0; i < 8; i++) {
            Arguments::Writer writer;
            writer.beginStruct();
                writer.writeByte(123);
                writer.beginArray(i ? Arguments::Writer::NonEmptyArray
                                    : Arguments::Writer::WriteTypesOfEmptyArray);
                for (int j = 0; j < std::max(i, 1); j++) {
                    writer.writeUint16(52345);
                }
                writer.endArray();
                writer.writeByte(123);
            writer.endStruct();
            TEST(writer.state() != Arguments::InvalidData);
            Arguments arg = writer.finish();
            TEST(writer.state() == Arguments::Finished);
            doRoundtrip(arg, false);
        }
    }
    for (int i = 0; i < 4; i++) {
        // Test RestartEmptyArrayToWriteTypes and writing an empty array inside the >1st iteration of another array
        Arguments::Writer writer;
        writer.beginArray((i & 2) ? Arguments::Writer::WriteTypesOfEmptyArray : Arguments::Writer::NonEmptyArray);
            // v don't care, the logic error is only in the second iteration
            writer.beginArray(Arguments::Writer::NonEmptyArray);
                writer.writeString(cstring("a"));
            writer.endArray();
            if (i & 1) {
                writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
            } else {
                writer.beginArray(Arguments::Writer::NonEmptyArray);
                writer.beginArray(Arguments::Writer::RestartEmptyArrayToWriteTypes);
            }
                    writer.writeString(cstring("a"));
            writer.endArray();
        writer.endArray();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    for (int i = 0; i < 3; i++) {
        // Test arrays inside empty arrays and especially peekPrimitiveArray / readPrimitiveArray
        Arguments::Writer writer;
        const bool outerEmpty = i > 1;
        const bool innerEmpty = i > 0;
        writer.beginArray(outerEmpty ? Arguments::Writer::WriteTypesOfEmptyArray
                                     : Arguments::Writer::NonEmptyArray);
        writer.beginArray(innerEmpty ? Arguments::Writer::WriteTypesOfEmptyArray
                                     : Arguments::Writer::NonEmptyArray);
        // Iterating several times through an empty array is allowed while writing
        writer.writeUint64(1234);
        writer.writeUint64(1234);
        TEST(writer.state() != Arguments::InvalidData);
        writer.endArray();
        writer.endArray();
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        {
            Arguments::Reader reader(arg);
            reader.beginArray();
            if (outerEmpty) {
                TEST(reader.state() == Arguments::EndArray);
                reader.endArray();
            } else {
                TEST(reader.state() == Arguments::BeginArray); // the inner array
                reader.beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
                TEST(reader.state() == Arguments::Uint64);
                reader.readUint64();
                if (!innerEmpty) {
                    reader.readUint64();
                }
                TEST(reader.state() == Arguments::EndArray);
                reader.endArray();
                reader.endArray();
            }
            TEST(reader.state() == Arguments::Finished);
        }
        {
            Arguments::Reader reader(arg);
            TEST(reader.peekPrimitiveArray(Arguments::Reader::ReadTypesOnlyIfEmpty) == Arguments::BeginArray);
            reader.beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
            TEST(reader.state() == Arguments::BeginArray);
            if (innerEmpty) {
                TEST(reader.peekPrimitiveArray() == Arguments::BeginArray);
            } else {
                TEST(reader.peekPrimitiveArray() == Arguments::Uint64);
            }
            TEST(reader.peekPrimitiveArray(Arguments::Reader::ReadTypesOnlyIfEmpty) == Arguments::Uint64);

            std::pair<Arguments::IoState, chunk> array = reader.readPrimitiveArray();
            TEST(array.first == Arguments::Uint64);
            if (innerEmpty) {
                TEST(array.second.ptr == nullptr);
                TEST(array.second.length == 0);
            } else {
                TEST(array.second.length == 2 * sizeof(uint64));
            }
            reader.endArray();
            TEST(reader.state() == Arguments::Finished);
        }
    }
    {
        for (int i = 0; i <= 32; i++) {
            Arguments::Writer writer;
            for (int j = 0; j <= i; j++) {
                writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
                if (j == 32) {
                    TEST(writer.state() == Arguments::InvalidData);
                }
            }
            if (i == 32) {
                TEST(writer.state() == Arguments::InvalidData);
                break;
            }
            writer.writeUint16(52345);
            for (int j = 0; j <= i; j++) {
                writer.endArray();
            }
            TEST(writer.state() != Arguments::InvalidData);
            Arguments arg = writer.finish();
            TEST(writer.state() == Arguments::Finished);
            doRoundtrip(arg, false);
        }
    }

    // Dicts

    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        maybeBeginDictEntry(&writer);
        writer.writeByte(0);
        writer.writeString(cstring("a"));
        maybeEndDictEntry(&writer);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        maybeBeginDictEntry(&writer);
        writer.writeString(cstring("a"));
        writer.beginVariant();
        writer.endVariant();
        maybeEndDictEntry(&writer);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        maybeBeginDictEntry(&writer);
        writer.writeString(cstring("a"));
        writer.beginVariant();
        writer.endVariant();
        maybeEndDictEntry(&writer);
        maybeBeginDictEntry(&writer);
        writer.writeString(cstring("a"));
        writer.beginVariant();
        writer.endVariant();
        maybeEndDictEntry(&writer);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        Arguments::Writer writer;
        writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
        maybeBeginDictEntry(&writer);
        writer.writeString(cstring("a"));
        writer.beginVariant();
        TEST(writer.state() != Arguments::InvalidData);
        writer.writeByte(0);
        // variants in nil arrays may contain data but it will be discarded, i.e. there will only be an
        // empty variant in the output
        writer.endVariant();
        maybeEndDictEntry(&writer);
        writer.endDict();
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    for (int i = 0; i < 4; i++) {
        // Test RestartEmptyArrayToWriteTypes and writing an empty dict inside the >1st iteration of another dict
        Arguments::Writer writer;
        writer.beginDict((i & 2) ? Arguments::Writer::WriteTypesOfEmptyArray : Arguments::Writer::NonEmptyArray);
            maybeBeginDictEntry(&writer);
            writer.writeString(cstring("a"));
            // v don't care, the logic error is only in the second iteration
            writer.beginDict(Arguments::Writer::NonEmptyArray);
                maybeBeginDictEntry(&writer);
                writer.writeString(cstring("a"));
                writer.writeInt32(1234);
                maybeEndDictEntry(&writer);
            writer.endDict();
            maybeEndDictEntry(&writer);
            maybeBeginDictEntry(&writer);
            writer.writeString(cstring("a"));
            if (i & 1) {
                writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
                    maybeBeginDictEntry(&writer);
            } else {
                writer.beginDict(Arguments::Writer::NonEmptyArray);
                writer.beginDict(Arguments::Writer::RestartEmptyArrayToWriteTypes);
                    maybeBeginDictEntry(&writer);
            }
                    writer.writeString(cstring("a"));
                    writer.writeInt32(1234);
                    maybeEndDictEntry(&writer);
            writer.endDict();
            maybeEndDictEntry(&writer);
        writer.endDict();
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
        doRoundtrip(arg, false);
    }
    {
        for (int i = 0; i <= 32; i++) {
            Arguments::Writer writer;
            for (int j = 0; j <= i; j++) {
                writer.beginDict(Arguments::Writer::WriteTypesOfEmptyArray);
                    maybeBeginDictEntry(&writer);
                if (j == 32) {
                    TEST(writer.state() == Arguments::InvalidData);
                }
                writer.writeUint16(12345);
            }
            if (i == 32) {
                TEST(writer.state() == Arguments::InvalidData);
                break;
            }
            writer.writeUint16(52345);
            for (int j = 0; j <= i; j++) {
                maybeEndDictEntry(&writer);
                writer.endDict();
            }
            TEST(writer.state() != Arguments::InvalidData);
            Arguments arg = writer.finish();
            TEST(writer.state() == Arguments::Finished);
            doRoundtrip(arg, false);
        }
    }
}

static void test_fileDescriptors()
{
#ifdef __unix__
    Arguments::Writer writer;
    writer.writeUnixFd(200);
    writer.writeByte(12);
    writer.writeUnixFd(1);
    Arguments arg = writer.finish();
    doRoundtrip(arg, false);
    // doRoundtrip only checks the serialized data, but unfortunately file descriptors
    // are out of band, so check explicitly
    Arguments::Reader reader(arg);
    TEST(reader.readUnixFd() == 200);
    TEST(reader.readByte() == 12);
    TEST(reader.readUnixFd() == 1);
    TEST(reader.state() == Arguments::Finished);
#endif
}

static void test_closeWrongAggregate()
{
    for (int i = 0; i < 8; i++) {
        for (int j = 0; j < 4; j++) {
            Arguments::Writer writer;
            switch (i % 4) {
            case 0: writer.beginStruct(); break;
            case 1: writer.beginVariant(); break;
            case 2: writer.beginArray(); break;
            case 3: writer.beginDict(); break;
            }

            if (i < 4) {
                writer.writeByte(123);
                if (i == 3) {
                    writer.writeByte(123); // value for dict
                }
            }

            switch (j) {
            case 0: writer.endStruct(); break;
            case 1: writer.endVariant(); break;
            case 2: writer.endArray(); break;
            case 3: writer.endDict(); break;
            }

            const bool isValid = writer.state() != Arguments::InvalidData;
            TEST(isValid == (i == j));
        }
    }
}

// TODO: test where we compare data and signature lengths of all combinations of zero/nonzero array
//       length and long/short type signature, to make sure that the signature is written but not
//       any data if the array is zero-length.

// TODO test empty dicts, too

int main(int, char *[])
{
    test_stringValidation();
    test_nesting();
    test_roundtrip();
    test_writerMisuse();
    // TODO test arrays where array length does not align with end of an element
    //      (corruption of serialized data)
    test_complicated();
    test_alignment();
    test_arrayOfVariant();
    test_realMessage();
    test_isWritingSignatureBug();
    test_primitiveArray();
    test_signatureLengths();
    test_emptyArrayAndDict();
    test_fileDescriptors();

    // TODO (maybe): specific tests for begin/endDictEntry() for both Reader and Writer.

    // TODO more misuse tests for Writer and maybe some for Reader
    test_closeWrongAggregate();

    std::cout << "Passed!\n";
}
