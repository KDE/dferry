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

#ifndef ARGUMENTLIST_H
#define ARGUMENTLIST_H

#include "export.h"
#include "types.h"

#include <cassert>
#include <string>
#include <vector>

class Error;
class Message;

class DFERRY_EXPORT ArgumentList
{
public:
    ArgumentList(); // constructs an empty argument list
    // constructs an argument list to deserialize data in @p data with signature @p signature;
    // if memOwnership is non-null, this means that signature and data's memory is contained in
    // a malloc()ed block starting at memOwnership, and ~ArgumentList will free it.
    // Otherwise, the instance assumes that @p signature and @p data live in "borrowed" memory and
    // you need to make sure that the memory lives as long as the ArgumentList.
    // The copy contructor and assignment operator will always copy the data, so copying is safe
    // regarding memory correctness but has a significant performance impact.
    ArgumentList(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped = false);

    // use these wherever possible if you care at all about efficiency!!
    ArgumentList(ArgumentList &&other);
    ArgumentList &operator=(ArgumentList &&other);

    // copying needs special treatment due to the d-pointer
    ArgumentList(const ArgumentList &other);
    ArgumentList &operator=(const ArgumentList &other);

    ~ArgumentList();

    // error (if any) propagates to Message, so it is still available later
    Error error() const;

    std::string prettyPrint() const;

    cstring signature() const;
    chunk data() const;

    enum SignatureType {
        MethodSignature = 0,
        VariantSignature
    };

    static bool isStringValid(cstring string);
    static bool isObjectPathValid(cstring objectPath);
    static bool isObjectPathElementValid(cstring pathElement);
    static bool isSignatureValid(cstring signature, SignatureType type = MethodSignature);

    static const int maxSignatureLength = 255;

    enum IoState {
        // "exceptional" states
        NotStarted = 0,
        Finished,
        NeedMoreData, // recoverable by adding data; should only happen when parsing the not length-prefixed variable message header
        InvalidData, // non-recoverable
        // Writer states when the next type is still open (not iterating in an array or dict)
        // ### it is inconsistent to have DictKey, but nothing for other constraints. The name AnyData is
        //     also weird. Remove DictKey and call AnyData InputData?
        AnyData, // occurs in Writer when you are free to add any type
        DictKey, // occurs in Writer when the next type must be suitable for a dict key -
                 // a simple string or numeric type.

        // the following occur in Reader, and in Writer when in the second or higher iteration
        // of an array or dict where the types must match the first iteration (except inside variants).

        // states pertaining to aggregates
        BeginArray,
        NextArrayEntry,
        EndArray,
        BeginDict,
        NextDictEntry, // 10
        EndDict,
        BeginStruct,
        EndStruct,
        BeginVariant,
        EndVariant,
        // the next element is plain data
        Byte,
        Boolean,
        Int16,
        Uint16,
        Int32, // 20
        Uint32,
        Int64,
        Uint64,
        Double,
        String,
        ObjectPath,
        Signature,
        UnixFd
    };

private:

    struct podCstring // Same as cstring but without ctor.
                      // Can't put the cstring type into a union because it has a constructor :/
    {
        byte *begin;
        uint32 length;
    };

    typedef union
    {
        byte Byte;
        bool Boolean;
        int16 Int16;
        uint16 Uint16;
        int32 Int32;
        uint32 Uint32;
        int64 Int64;
        uint64 Uint64;
        double Double;
        podCstring String; // also for ObjectPath and Signature
    } DataUnion;

public:

    // error handling is done by asking state() or isError(), not by method return values.
    // occasionally looking at isError() is less work than checking every call.
    class Reader
    {
    public:
        explicit Reader(const ArgumentList &al);
        explicit Reader(const Message &msg);
        Reader(Reader &&other);
        void operator=(Reader &&other);
        Reader(const Reader &other) = delete;
        void operator=(const Reader &other) = delete;

        ~Reader();

        bool isValid() const;
        Error error() const; // see also: aggregateStack()

        IoState state() const { return m_state; }
        cstring stateString() const;
         // HACK call this in NeedMoreData state when more data has been added; this replaces m_data
         // ### will need to fix up any VariantInfo::prevSignature on the stack where prevSignature
         //     is inside m_data; length will still work but begin will be outdated.
        void replaceData(chunk data); // TODO move this to ArgumentList

        bool isFinished() const { return m_state == Finished; }
        bool isError() const { return m_state == InvalidData || m_state == NeedMoreData; } // TODO remove

        // when @p isEmpty is not null and the array contains no elements, the array is
        // iterated over once so you can get the type information. due to lack of data,
        // all contained containers (arrays, dicts and variants) will contain no elements,
        // but you can iterate once over them as well to obtain type information.
        // any values returned by read... will be garbage.
        // in any case, *isEmpty will be set to indicate whether the array is empty.

        void beginArray(bool *isEmpty = 0);
        // call this before reading each entry; when it returns false the array has ended.
        // TODO implement & document that all values returned by read... are zero/null?
        bool nextArrayEntry();
        void endArray(); // leaves the current array; only  call this in state EndArray!

        void beginDict(bool *isEmpty = 0);
        bool nextDictEntry(); // like nextArrayEntry()
        void endDict(); // like endArray()

        void beginStruct();
        void endStruct(); // like endArray()

        void beginVariant();
        void endVariant(); // like endArray()

        std::vector<IoState> aggregateStack() const; // the aggregates the reader is currently in

        // reading a type that is not indicated by state() will cause undefined behavior and at
        // least return garbage.
        byte readByte() { byte ret = m_u.Byte; advanceState(); return ret; }
        bool readBoolean() { bool ret = m_u.Boolean; advanceState(); return ret; }
        int16 readInt16() { int ret = m_u.Int16; advanceState(); return ret; }
        uint16 readUint16() { uint16 ret = m_u.Uint16; advanceState(); return ret; }
        int32 readInt32() { int32 ret = m_u.Int32; advanceState(); return ret; }
        uint32 readUint32() { uint32 ret = m_u.Uint32; advanceState(); return ret; }
        int64 readInt64() { int64 ret = m_u.Int64; advanceState(); return ret; }
        uint64 readUint64() { uint64 ret = m_u.Uint64; advanceState(); return ret; }
        double readDouble() { double ret = m_u.Double; advanceState(); return ret; }
        cstring readString() { cstring ret(m_u.String.begin, m_u.String.length); advanceState(); return ret; }
        cstring readObjectPath() { cstring ret(m_u.String.begin, m_u.String.length); advanceState(); return ret; }
        cstring readSignature() { cstring ret(m_u.String.begin, m_u.String.length); advanceState(); return ret; }
        uint32 readUnixFd() { uint32 ret = m_u.Uint32; advanceState(); return ret; }

    private:
        class Private;
        friend class Private;
        void beginRead();
        void doReadPrimitiveType();
        void doReadString(int lengthPrefixSize);
        void advanceState();
        void advanceStateFrom(IoState expectedState);
        void beginArrayOrDict(bool isDict, bool *isEmpty);
        bool nextArrayOrDictEntry(bool isDict);

        Private *d;

        // two data members not behind d-pointer for performance reasons, especially inlining
        IoState m_state;

        // it is more efficient, in code size and performance, to read the data in advanceState()
        // and store the result for later retrieval in readFoo()
        DataUnion m_u;
    };

    class Writer
    {
    public:
        explicit Writer();
        Writer(Writer &&other);
        void operator=(Writer &&other);
        Writer(const Writer &other) = delete;
        void operator=(const Writer &other) = delete;

        ~Writer();

        bool isValid() const;
        // error propagates to ArgumentList (if the error wasn't that the ArgumentList is not writable),
        // so it is still available later
        Error error() const; // see also: aggregateStack()

        IoState state() const { return m_state; }
        cstring stateString() const;

        void beginArray(bool isEmpty);
        // call this before writing each entry; calling it before the first entry is optional for
        // the convenience of client code.
        void nextArrayEntry();
        void endArray();

        void beginDict(bool isEmpty);
        void nextDictEntry();
        void endDict();

        void beginStruct();
        void endStruct();

        void beginVariant();
        void endVariant();

        ArgumentList finish();

        std::vector<IoState> aggregateStack() const; // the aggregates the writer is currently in

        void writeByte(byte b);
        void writeBoolean(bool b);
        void writeInt16(int16 i);
        void writeUint16(uint16 i);
        void writeInt32(int32 i);
        void writeUint32(uint32 i);
        void writeInt64(int64 i);
        void writeUint64(uint64 i);
        void writeDouble(double d);
        void writeString(cstring string);
        void writeObjectPath(cstring objectPath);
        void writeSignature(cstring signature);
        void writeUnixFd(uint32 fd);

    private:
        class Private;
        friend class Private;

        void doWritePrimitiveType(uint32 alignAndSize);
        void doWriteString(int lengthPrefixSize);
        void advanceState(chunk signatureFragment, IoState newState);
        void beginArrayOrDict(bool isDict, bool isEmpty);
        void nextArrayOrDictEntry(bool isDict);
        void finishInternal();

        Private *d;

        // two data members not behind d-pointer for performance reasons
        IoState m_state;

        // ### check if it makes any performance difference to have this here (writeFoo() should benefit)
        DataUnion m_u;
    };

private:
    class Private;
    Private *d;
};

#endif // ARGUMENTLIST_H
