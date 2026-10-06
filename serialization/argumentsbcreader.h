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

#ifndef ARGUMENTSBCREADER_H
#define ARGUMENTSBCREADER_H

#include "arguments.h"

union FerCode;

// error handling is done by asking state() or isError(), not by method return values.
// occasionally looking at isError() is less work than checking every call.
class DFERRY_EXPORT ArgumentsBcReader
{
public:
    explicit ArgumentsBcReader(const Arguments &args,
                               Arguments::FerEncodeOptions encodeOptions = Arguments::FerEncodeOptions::None);
    explicit ArgumentsBcReader(const Message &msg,
                               Arguments::FerEncodeOptions encodeOptions = Arguments::FerEncodeOptions::None);
#ifdef HAVE_BOOST
    ArgumentsBcReader(const Arguments &args, boost::local_shared_ptr<std::vector<FerCode>> ferCode);
    ArgumentsBcReader(const Message &msg, boost::local_shared_ptr<std::vector<FerCode>> ferCode);
#else
    ArgumentsBcReader(const Arguments &args, std::shared_ptr<std::vector<FerCode>> ferCode);
    ArgumentsBcReader(const Message &msg, std::shared_ptr<std::vector<FerCode>> ferCode);
#endif
    ArgumentsBcReader(ArgumentsBcReader &&other);
    ArgumentsBcReader(const ArgumentsBcReader &other);

    ~ArgumentsBcReader();

    void operator=(ArgumentsBcReader &&other);
    // TODO unit-test copy and assignment

    void operator=(const ArgumentsBcReader &other);

    bool isValid() const;
    Error error() const;

    void setValidatesUtf8(bool validate);
    bool validatesUtf8() const;

    Arguments::IoState state() const { return m_state; }
    //cstring stateString() const;

    bool isFinished() const { return m_state == Arguments::Finished; }
    bool isError() const { return m_state == Arguments::InvalidData; } // TODO remove?

    bool beginArray();
    //void skipArray(); // skips the current array; only  call this in state BeginArray!
    void endArray(); // leaves the current array; only  call this in state EndArray!

    bool beginDict();
    //void skipDict(); // like skipArray()
    //bool isDictKey() const; // this can be used to track whether the current value is a dict key or value, e.g.
                            // for pretty-printing purposes (it is usually clear in marshalling code).
    void endDict(); // like endArray()

    void beginStruct();
    //void skipStruct(); // like skipArray()
    void endStruct(); // like endArray()

    void beginVariant(); // TODO probably need to return a regular Reader here - or somehow obtain bytecode
                         // for the variant signature as well and swap state...
    // Reader beginFlexVariant(); // TODO?
    //void skipVariant(); // like skipArray();
    void endVariant(); // like endArray()

    //std::vector<IoState> aggregateStack() const; // the aggregates the reader is currently in
    //uint32 aggregateDepth() const; // like calling aggregateStack().size() but much faster
    //IoState currentAggregate() const; // the innermost aggregate, NotStarted if not in an aggregate

    // reading a type that is not indicated by state() will cause undefined behavior and at
    // least return garbage.
    byte readByte()     { return *reinterpret_cast<const byte *  >(advanceState(Arguments::Byte)); }
    // Yes, booleans are stored as a uint32 with possible values 0 and 1 in DBus!
    bool readBoolean()  { return *reinterpret_cast<const uint32 *>(advanceState(Arguments::Boolean)); }
    int16 readInt16()   { return *reinterpret_cast<const int16 * >(advanceState(Arguments::Int16)); }
    uint16 readUint16() { return *reinterpret_cast<const uint16 *>(advanceState(Arguments::Uint16)); }
    int32 readInt32()   { return *reinterpret_cast<const int32 * >(advanceState(Arguments::Int32)); }
    uint32 readUint32() { return *reinterpret_cast<const uint32 *>(advanceState(Arguments::Uint32)); }
    int64 readInt64()   { return *reinterpret_cast<const int64 * >(advanceState(Arguments::Int64)); }
    uint64 readUint64() { return *reinterpret_cast<const uint64 *>(advanceState(Arguments::Uint64)); }
    double readDouble() { return *reinterpret_cast<const double *>(advanceState(Arguments::Double)); }
    cstring readString()
    {
        const void* retPtr = advanceState(Arguments::String);
        return cstring(reinterpret_cast<const char *>(retPtr) + sizeof(uint32),
                       *reinterpret_cast<const uint32 *>(retPtr));
    }
    cstring readObjectPath()
    {
        const void* retPtr = advanceState(Arguments::ObjectPath);
        return cstring(reinterpret_cast<const char *>(retPtr) + sizeof(uint32),
                       *reinterpret_cast<const uint32 *>(retPtr));
    }
    cstring readSignature()
    {
        const void* retPtr = advanceState(Arguments::Signature);
        return cstring(reinterpret_cast<const char *>(retPtr) + sizeof(byte),
                       *reinterpret_cast<const byte *>(retPtr));
    }
    int readUnixFd();

    // Returns primitive type and the raw array data if in BeginArray state of an array containing only a
    // primitive type. You must copy the data before destroying the BcReader or changing its backing store
    // with replaceData().
    // If the array is empty, that does not constitute a special case with this function: It will return
    // the type in the first return value as usual and an empty chunk in the second return value.
    // (### it might be possible to extend this feature to all fixed-length types including structs)
    //std::pair<Arguments::IoState, chunk> readPrimitiveArray();

#ifdef WITH_DICT_ENTRY
    void beginDictEntry();
    void endDictEntry();
#endif

private:
    void beginRead();
    //void doReadPrimitiveType();
    void doReadString(uint32 lengthPrefixSize);
    bool beginArrayInternal();
    const void *advanceState(Arguments::IoState expectedState);
    //void beginArrayOrDict(bool isDict, EmptyArrayOption option);
    //void skipArrayOrDictSignature(bool isDict);
    //void skipArrayOrDict(bool isDict);

    Arguments::IoState m_state;

    class Private;
    friend class Private;
    Private *d;
};

#endif // ARGUMENTSBCREADER_H
