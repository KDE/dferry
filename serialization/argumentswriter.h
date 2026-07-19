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

#ifndef ARGUMENTSWRITER_H
#define ARGUMENTSWRITER_H

#include "arguments.h"

class DFERRY_EXPORT ArgumentsWriter
{
public:
    explicit ArgumentsWriter();
    ArgumentsWriter(ArgumentsWriter &&other);
    void operator=(ArgumentsWriter &&other);
    // TODO unit-test copy and assignment
    ArgumentsWriter(const ArgumentsWriter &other);
    void operator=(const ArgumentsWriter &other);

    ~ArgumentsWriter();

    bool isValid() const;
    // error propagates to Arguments (if the error wasn't that the Arguments is not writable),
    // so it is still available later
    Error error() const; // see also: aggregateStack()

    Arguments::IoState state() const { return m_state; }
    cstring stateString() const;
    bool isInsideEmptyArray() const;
    cstring currentSignature() const; // current signature, either main signature or current variant
    uint32 currentSignaturePosition() const;

    enum ArrayOption
    {
        NonEmptyArray = 0,
        WriteTypesOfEmptyArray,
        RestartEmptyArrayToWriteTypes
    };

    void beginArray(ArrayOption option = NonEmptyArray);
    void endArray();

    void beginDict(ArrayOption option = NonEmptyArray);
    void endDict();

    void beginStruct();
    void endStruct();

    void beginVariant();
    void endVariant();

    Arguments finish();

    std::vector<Arguments::IoState> aggregateStack() const; // the aggregates the writer is currently in
    uint32 aggregateDepth() const; // like calling aggregateStack().size() but much faster
    Arguments::IoState currentAggregate() const; // the innermost aggregate, NotStarted if not in an aggregate

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
    void writeUnixFd(int32 fd);

    void writePrimitiveArray(Arguments::IoState type, chunk data);

    // Return the current serialized data; if the current state of writing has any aggregates open
    // OR is in an error state, return an empty chunk (instead of invalid serialized data).
    // After (or before - this method is const!) an empty chunk is returned, you can find out why
    // using state(), isValid(), and currentAggregate().
    // The returned memory is only valid as long as the ArgumentsWriter is not mutated in any way!
    // If successful, the returned data can be used together with currentSignature() and
    // fileDescriptors() to construct a temporary Arguments as a strucrured view into the data.
    chunk peekSerializedData() const;
    const std::vector<int> &fileDescriptors() const;

#ifdef WITH_DICT_ENTRY
    void beginDictEntry();
    void endDictEntry();
#endif

    class Private;

private:
    friend class MessagePrivate;
    void writeVariantForMessageHeader(char sig); // faster variant for typical message headers;
    // does not work for nested variants which aren't needed for message headers. Also does not
    // change the aggregate stack, but Message knows how to handle it.
    void fixupAfterWriteVariantForMessageHeader();

    void doWritePrimitiveType(Arguments::IoState type, uint32 alignAndSize);
    void doWriteString(Arguments::IoState type, uint32 lengthPrefixSize);
    void advanceState(cstring signatureFragment, Arguments::IoState newState);
    void beginArrayOrDict(Arguments::IoState beginWhat, ArrayOption option);
    void flushQueuedData();

    Private *d;

    // two data members not behind d-pointer for performance reasons
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

    // ### check if it makes any performance difference to have this here (writeFoo() should benefit)
    DataUnion m_u;
};

#endif // ARGUMENTSWRITER_H
