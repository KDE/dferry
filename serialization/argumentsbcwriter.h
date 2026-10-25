// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ARGUMENTSBCWRITER_H
#define ARGUMENTSBCWRITER_H

#include "arguments.h"

union FerCode;

// error handling is done by asking state() or isError(), not by method return values.
// occasionally looking at isError() is less work than checking every call.
class DFERRY_EXPORT ArgumentsBcWriter
{
public:
    explicit ArgumentsBcWriter(cstring signature,
                               Arguments::FerEncodeOptions encodeOptions = Arguments::FerEncodeOptions::None);
#ifdef HAVE_BOOST
    ArgumentsBcWriter(cstring signature, boost::local_shared_ptr<std::vector<FerCode>> ferCode);
#else
    ArgumentsBcWriter(cstring signature, std::shared_ptr<std::vector<FerCode>> ferCode);
#endif
    ArgumentsBcWriter(ArgumentsBcWriter &&other);
    ArgumentsBcWriter(const ArgumentsBcWriter &other);

    ~ArgumentsBcWriter();

    void operator=(ArgumentsBcWriter &&other);
    // TODO unit-test copy and assignment

    void operator=(const ArgumentsBcWriter &other);

    Arguments::IoState state() const { return m_state; }
    cstring stateString() const;

    Error error() const;

    bool isError() const;
    bool isFinished() const;
    bool isSuccess() const;

    void setValidatesUtf8(bool validate);
    bool validatesUtf8() const;

    void beginArray();
    void endArray();

    void beginDict();
    void endDict();

    void beginStruct() { advanceStateAggregate(Arguments::BeginStruct); }
    //void skipStruct(); // like skipArray()
    void endStruct() { advanceStateAggregate(Arguments::EndStruct); } // like endArray()

    void beginVariant(); // TODO probably need to return a regular Reader here - or somehow obtain bytecode
                         // for the variant signature as well and swap state...
    // Reader beginFlexVariant(); // TODO?
    //void skipVariant(); // like skipArray();
    void endVariant(); // like endArray()

    Arguments finish();

    // reading a type that is not indicated by state() will cause undefined behavior and at
    // least return garbage.
    void writeByte(byte arg)     { advanceStatePrimitive(Arguments::Byte, static_cast<uint64>(arg)); }
    // Yes, booleans are stored as a uint32 with possible values 0 and 1 in DBus!
    void writeBoolean(bool arg)  { advanceStatePrimitive(Arguments::Boolean, static_cast<uint64>(arg)); }
    void writeInt16(int16 arg)   { advanceStatePrimitive(Arguments::Int16, static_cast<uint64>(arg)); }
    void writeUint16(uint16 arg) { advanceStatePrimitive(Arguments::Uint16, static_cast<uint64>(arg)); }
    void writeInt32(int32 arg)   { advanceStatePrimitive(Arguments::Int32, static_cast<uint64>(arg)); }
    void writeUint32(uint32 arg) { advanceStatePrimitive(Arguments::Uint32, static_cast<uint64>(arg)); }
    void writeInt64(int64 arg)   { advanceStatePrimitive(Arguments::Int64, static_cast<uint64>(arg)); }
    void writeUint64(uint64 arg) { advanceStatePrimitive(Arguments::Uint64, static_cast<uint64>(arg)); }
    void writeDouble(double arg)
    {
        // *reinterpret_cast<uint64 *>(&arg) is UB, so use "the memcpy trick" which we expect the compiler
        // to optimize out
        uint64 intArg;
        static_assert(sizeof(intArg) == sizeof(arg));
        memcpy(&intArg, &arg, sizeof(arg));
        advanceStatePrimitive(Arguments::Double, *reinterpret_cast<uint64 *>(&intArg));

    }
    void writeString(cstring arg)
    {
        advanceStateString(Arguments::String, arg);
    }
    void writeObjectPath(cstring arg)
    {
        advanceStateString(Arguments::ObjectPath, arg);
    }
    void writeSignature(cstring arg)
    {
        advanceStateString(Arguments::Signature, arg);
    }
    void writeUnixFd(int);

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
    void beginWrite(cstring signature);
    void beginArrayInternal(bool isDict);
    void endArrayInternal(bool isDict);

    enum class StateType{Primitive, String, Aggregate};
    template<StateType stateType>
    #ifdef __GNUC__
    [[gnu::always_inline]]
    #elif defined __MSVC__
    [[msvc::forceinline]]
    #endif
    inline void advanceStateInternal(Arguments::IoState expectedState, uint64 arg, uint32 arg2);
    void advanceStatePrimitive(Arguments::IoState expectedState, uint64 arg);
    void advanceStateString(Arguments::IoState expectedState, cstring arg);
    void advanceStateAggregate(Arguments::IoState expectedState);

    Arguments::IoState m_state;

    class Private;
    friend class Private;
    Private *d;
};

#endif // ARGUMENTSBCWRITER_H
