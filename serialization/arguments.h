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

#ifndef ARGUMENTS_H
#define ARGUMENTS_H

#include "export.h"
#include "types.h"

#include <cassert>
#include <string>
#include <vector>

class Error;
class Message;
class MessagePrivate;

//#define WITH_DICT_ENTRY

class DFERRY_EXPORT Arguments
{
public:
    class Reader;
    class Writer;

    enum SignatureType
    {
        MethodSignature = 0,
        VariantSignature
    };

    enum
    {
        MaxSignatureLength = 255
    };

    enum IoState
    {
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
        EndArray,
        BeginDict,
        EndDict,
        BeginStruct, // 10
        EndStruct,
        BeginVariant,
        EndVariant,
        // the next element is plain data
        Boolean,
        Byte,
        Int16,
        Uint16,
        Int32,
        Uint32,
        Int64, // 20
        Uint64,
        Double,
        String,
        ObjectPath,
        Signature,
        UnixFd,
#ifdef WITH_DICT_ENTRY
        BeginDictEntry,
        EndDictEntry,
#endif
        LastState
    };

    Arguments(); // constructs an empty argument list
    // constructs an argument list to deserialize data in @p data with signature @p signature;
    // if memOwnership is non-null, this means that signature and data's memory is contained in
    // a malloc()ed block starting at memOwnership, and ~Arguments will free it.
    // Otherwise, the instance assumes that @p signature and @p data live in "borrowed" memory and
    // you need to make sure that the memory lives as long as the Arguments.
    // (A notable user of this is Message - you can only get a const ref to its internal Arguments
    //  so you need to copy to take the Arguments away from the Message, which copies out of the
    //  borrowed memory into heap memory so the copy is safe)
    // The copy contructor and assignment operator will always copy the data, so copying is safe
    // regarding memory correctness but has a significant performance impact.
    Arguments(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped = false);
    // same thing as above, just with file descriptors
    Arguments(byte *memOwnership, cstring signature, chunk data,
              std::vector<int> fileDescriptors, bool isByteSwapped = false);

    // use these wherever possible if you care at all about efficiency!!
    Arguments(Arguments &&other);
    Arguments &operator=(Arguments &&other);

    // copying needs special treatment due to the d-pointer
    Arguments(const Arguments &other);
    Arguments &operator=(const Arguments &other);

    ~Arguments();

    // error (if any) propagates to Message, so it is still available later
    Error error() const;

    std::string prettyPrint() const;

    cstring signature() const;
    chunk data() const;
    const std::vector<int> &fileDescriptors() const;
    bool isByteSwapped() const;

    static bool isStringValid(cstring string);
    static bool isObjectPathValid(cstring objectPath);
    static bool isObjectPathElementValid(cstring pathElement);
    static bool isSignatureValid(cstring signature, SignatureType type = MethodSignature);

    static void copyOneElement(Reader *reader, Writer *writer);

private:
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

public:
    // error handling is done by asking state() or isError(), not by method return values.
    // occasionally looking at isError() is less work than checking every call.
    class DFERRY_EXPORT Reader
    {
    public:
        explicit Reader(const Arguments &al);
        explicit Reader(const Message &msg);
        Reader(Reader &&other);
        void operator=(Reader &&other);
        // TODO unit-test copy and assignment
        Reader(const Reader &other);
        void operator=(const Reader &other);

        ~Reader();

        bool isValid() const;
        Error error() const; // see also: aggregateStack()

        IoState state() const { return m_state; }
        cstring stateString() const;
        bool isInsideEmptyArray() const;
        cstring currentSignature() const; // current signature, either main signature or current variant
        uint32 currentSignaturePosition() const;
        cstring currentSingleCompleteTypeSignature() const;
        // HACK call this in NeedMoreData state when more data has been added; this replaces m_data
        // WARNING: calling replaceData() invalidates copies (if any) of this Reader
        void replaceData(chunk data);

        bool isFinished() const { return m_state == Finished; }
        bool isError() const { return m_state == InvalidData || m_state == NeedMoreData; } // TODO remove

        enum EmptyArrayOption
        {
            SkipIfEmpty = 0,
            ReadTypesOnlyIfEmpty
        };

        // Start reading an array. @p option changes behavior in case the array is empty, i.e. it has
        // zero elements. Empty arrays still contain types, which may be of interest.
        // If @p option == SkipIfEmpty, empty arrays will work according to the usual rules:
        // you call nextArrayEntry() and it returns false, you call endArray() and proceed to the next
        // value or aggregate.
        // If @p option == ReadTypesOnlyIfEmpty, you will be taken on a single iteration through the array
        // if it is empty, which makes it possible to extract the type(s) of data inside the array. In
        // that mode, all data returned from read...() is undefined and should be discarded. Only use state()
        // to get the types and call read...() purely to move from one type to the next.
        // Empty arrays are handled that way for symmetry with regular data extraction code so that very
        // similar code can handle empty and nonempty arrays.
        //
        // The return value is false if the array is empty (has 0 elements), true if it has >= 1 elements.
        // The return value is not affected by @p option.
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

        std::vector<IoState> aggregateStack() const; // the aggregates the reader is currently in
        uint32 aggregateDepth() const; // like calling aggregateStack().size() but much faster
        IoState currentAggregate() const; // the innermost aggregate, NotStarted if not in an aggregate

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
        cstring readString() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
        cstring readObjectPath() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
        cstring readSignature() { cstring ret(m_u.String.ptr, m_u.String.length); advanceState(); return ret; }
        int32 readUnixFd() { int32 ret = m_u.Int32; advanceState(); return ret; }

        void skipCurrentElement(); // works on single values and Begin... states. In the Begin... states,
                                   // skips the whole aggregate.

        // Returns primitive type and the raw array data if in BeginArray state of an array containing only a
        // primitive type. You must copy the data before destroying the Reader or changing its backing store
        // with replaceData().
        // If the array is empty, that does not constitute a special case with this function: It will return
        // the type in the first return value as usual and an empty chunk in the second return value.
        // (### it might be possible to extend this feature to all fixed-length types including structs)
        std::pair<Arguments::IoState, chunk> readPrimitiveArray();
        // In state BeginArray, check if the array is a primitive array, in order to check whether to use
        // readPrimitiveArray(). Returns a primitive type if readPrimitiveArray() will succeed, BeginArray
        // if the array is not primitive, InvalidData if state is not BeginArray. The latter will not put
        // the reader in InvalidData state.
        // If option is SkipIfEmpty, an empty array of primitives will result in a return value of BeginArray
        // instead of the type of primitive.
        Arguments::IoState peekPrimitiveArray(EmptyArrayOption option = SkipIfEmpty) const;

#ifdef WITH_DICT_ENTRY
        void beginDictEntry();
        void endDictEntry();
#endif

        class Private;
        friend class Private;

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
        IoState m_state;

        // it is more efficient, in code size and performance, to read the data in advanceState()
        // and store the result for later retrieval in readFoo()
        DataUnion m_u;
    };

    class DFERRY_EXPORT Writer
    {
    public:
        explicit Writer();
        Writer(Writer &&other);
        void operator=(Writer &&other);
        // TODO unit-test copy and assignment
        Writer(const Writer &other);
        void operator=(const Writer &other);

        ~Writer();

        bool isValid() const;
        // error propagates to Arguments (if the error wasn't that the Arguments is not writable),
        // so it is still available later
        Error error() const; // see also: aggregateStack()

        IoState state() const { return m_state; }
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

        std::vector<IoState> aggregateStack() const; // the aggregates the writer is currently in
        uint32 aggregateDepth() const; // like calling aggregateStack().size() but much faster
        IoState currentAggregate() const; // the innermost aggregate, NotStarted if not in an aggregate

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

        void writePrimitiveArray(IoState type, chunk data);

        // Return the current serialized data; if the current state of writing has any aggregates open
        // OR is in an error state, return an empty chunk (instead of invalid serialized data).
        // After (or before - this method is const!) an empty chunk is returned, you can find out why
        // using state(), isValid(), and currentAggregate().
        // The returned memory is only valid as long as the Writer is not mutated in any way!
        // If successful, the returned data can be used together with currentSignature() and
        // fileDescriptors() to construct a temporary Arguments as a strucrured view into the data.
        chunk peekSerializedData() const;
        const std::vector<int> &fileDescriptors() const;

#ifdef WITH_DICT_ENTRY
        void beginDictEntry();
        void endDictEntry();
#endif

        class Private;
        friend class Private;

    private:
        friend class MessagePrivate;
        void writeVariantForMessageHeader(char sig); // faster variant for typical message headers;
        // does not work for nested variants which aren't needed for message headers. Also does not
        // change the aggregate stack, but Message knows how to handle it.
        void fixupAfterWriteVariantForMessageHeader();

        void doWritePrimitiveType(IoState type, uint32 alignAndSize);
        void doWriteString(IoState type, uint32 lengthPrefixSize);
        void advanceState(cstring signatureFragment, IoState newState);
        void beginArrayOrDict(IoState beginWhat, ArrayOption option);
        void flushQueuedData();

        Private *d;

        // two data members not behind d-pointer for performance reasons
        IoState m_state;

        // ### check if it makes any performance difference to have this here (writeFoo() should benefit)
        DataUnion m_u;
    };

    class Private;

private:
    Private *d;
};

#endif // ARGUMENTS_H
