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

#ifndef ARGUMENTSREADER_H
#define ARGUMENTSREADER_H

#include "arguments.h"

//TODO make these more private, move to private header, move to Arguments... but they can't stay here!

// error handling is done by asking state() or isError(), not by method return values.
// occasionally looking at isError() is less work than checking every call.


class DFERRY_EXPORT ArgumentsReader
{
public:
    explicit ArgumentsReader(const Arguments &args);
    explicit ArgumentsReader(const Message &msg);
    ArgumentsReader(ArgumentsReader &&other);
    void operator=(ArgumentsReader &&other);
    // TODO unit-test copy and assignment
    ArgumentsReader(const ArgumentsReader &other);
    void operator=(const ArgumentsReader &other);

    ~ArgumentsReader();

    Arguments::IoState state() const { return m_state; }
    cstring stateString() const;

    bool isValid() const; // TODO what does it mean exactly?
    Error error() const;

    bool isError() const;
    bool isFinished() const;
    bool isSuccess() const;

    void setValidatesUtf8(bool validate);
    bool validatesUtf8() const;

    bool isInsideEmptyArray() const;
    cstring currentSignature() const;
    uint32 currentSignaturePosition() const;
    cstring currentSingleCompleteTypeSignature() const;

    /// What to do when opening an empty array
    enum EmptyArrayOption
    {
        SkipIfEmpty = 0, ///< Skip the array
        ReadTypesOnlyIfEmpty ///< Do one pass through the array to get the types
    };

    bool beginArray(EmptyArrayOption option = SkipIfEmpty);
    void skipArray(); // skips the current array; only  call this in state BeginArray!
    void endArray(); // leaves the current array; only  call this in state EndArray!

    bool beginDict(EmptyArrayOption option = SkipIfEmpty);
    void skipDict(); // like skipArray()
    bool isDictKey() const; // this can be used to track whether the current value is a dict key or value, e.g.
                            // for pretty-printing purposes (it is usually clear in marshalling code).
    void endDict(); // like endArray()

    void beginStruct();
    void skipStruct(); // like skipArray()
    void endStruct(); // like endArray()

    void beginVariant();
    void skipVariant(); // like skipArray();
    void endVariant(); // like endArray()

    std::vector<Arguments::IoState> aggregateStack() const; // the aggregates the reader is currently in
    uint32 aggregateDepth() const; // like calling aggregateStack().size() but much faster
    Arguments::IoState currentAggregate() const; // the innermost aggregate, NotStarted if not in an aggregate

    // reading a type that is not indicated by state() will cause undefined behavior and at
    // least return garbage.
    byte readByte() { byte ret = m_u.Byte; advanceState(); return ret; }
    bool readBoolean() { bool ret = m_u.Boolean; advanceState(); return ret; }
    int16 readInt16() { int16 ret = m_u.Int16; advanceState(); return ret; }
    uint16 readUint16() { uint16 ret = m_u.Uint16; advanceState(); return ret; }
    int32 readInt32() { int32 ret = m_u.Int32; advanceState(); return ret; }
    uint32 readUint32() { uint32 ret = m_u.Uint32; advanceState(); return ret; }
    int64 readInt64() { int64 ret = m_u.Int64; advanceState(); return ret; }
    uint64 readUint64() { uint64 ret = m_u.Uint64; advanceState(); return ret; }
    double readDouble() { double ret = m_u.Double; advanceState(); return ret; }
    cstring readString() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
    cstring readObjectPath() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
    cstring readSignature() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
    int32 readUnixFd() { int32 ret = m_u.Int32; advanceState(); return ret; }

    void skipCurrentElement();

    // (### it might be possible to extend this feature to all fixed-length types including structs)
    std::pair<Arguments::IoState, chunk> readPrimitiveArray();
    Arguments::IoState peekPrimitiveArray(EmptyArrayOption option = SkipIfEmpty) const;

#ifdef WITH_DICT_ENTRY
    void beginDictEntry();
    void endDictEntry();
#endif

    class Private;

private:
    void beginRead();
    void doReadPrimitiveType();
    void doReadString(uint32 lengthPrefixSize);
    void advanceState();
    void beginArrayOrDict(bool isDict, EmptyArrayOption option);
    void skipArrayOrDictSignature(bool isDict);
    void skipArrayOrDict(bool isDict);

    Private *d;

    // two data members not behind d-pointer for performance reasons, especially inlining
    Arguments::IoState m_state;


    struct podCstring // Same as cstring but without ctor.
                      // Can't put the cstring type into a union because it has a constructor :/
    {
        char *ptr;
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

    // it is more efficient, in code size and performance, to read the data in advanceState()
    // and store the result for later retrieval in readFoo()
    DataUnion m_u;
};

#endif // ARGUMENTSREADER_H
