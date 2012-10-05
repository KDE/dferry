#include "types.h"

#include <cassert>

#include <vector>

class Nesting; // TODO remove this when we've d-pointerized everything

class ArgumentList
{
public:
    ArgumentList(); // constructs an empty argument list
     // constructs an argument list to deserialize data in @p data with signature @p signature
    ArgumentList(cstring signature, array data, bool isByteSwapped = false);

    // valid when no write cursor is open on the instance

    // TODO rethink how to find out final message size
    int length() const;

     // returns true when at least one read cursor is open, false otherwise
    bool isReading() const { return m_readCursorCount; }
    // returns true when a write cursor is open, false otherwise
    bool isWriting() const { return m_hasWriteCursor; }

    cstring signature() const { return m_signature; }
    array data() const { return m_data; }

    class ReadCursor;
    class WriteCursor;
    ReadCursor beginRead();
    WriteCursor beginWrite();

    enum SignatureType {
        MethodSignature = 0,
        VariantSignature
    };

    static bool isStringValid(cstring string);
    static bool isObjectPathValid(cstring objectPath);
    static bool isSignatureValid(cstring signature, SignatureType type = MethodSignature);

    static const int maxSignatureLength = 255;

    enum CursorState {
        // "exceptional" states
        NotStarted = 0,
        Finished,
        NeedMoreData, // recoverable by adding data; should only happen when parsing the not length-prefixed variable message header
        InvalidData, // non-recoverable
        // WriteCursor states when the next type is still open (not iterating in an array or dict)
        AnyData, // occurs in WriteCursor when you are free to add any type
        DictKey, // occurs in WriteCursor when the next type must be suitable for a dict key -
                 // a simple string or numeric type.

        // the following occur in ReadCursor, and in WriteCursor when in the second or higher iteration
        // of an array or dict where the types must match the first iteration (except inside variants).

        // states pertaining to aggregates
        BeginArray,
        NextArrayEntry,
        EndArray,
        BeginDict,
        NextDictEntry,
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
        Int32,
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

public:
    // a cursor is similar to an iterator, but more tied to the underlying data structure
    // error handling is done by asking state() or isError(), not by method return values.
    // occasionally looking at isError() is less work than checking every call.
    class ReadCursor
    {
    public:
        ~ReadCursor();

        bool isValid() const { return m_argList; }

        CursorState state() const { return m_state; }
        cstring stateString() const;
         // HACK call this in NeedMoreData state when more data has been added; this replaces m_data
         // ### will need to fix up any VariantInfo::prevSignature on the stack where prevSignature
         //     is inside m_data; length will still work but begin will be outdated.
        void replaceData(array data);

        bool isFinished() const { return m_state == Finished; }
        bool isError() const { return m_state == InvalidData || m_state == NeedMoreData; }

        // when @p isEmpty is not null and the array contains no elements, the array is
        // iterated over once so you can get the type information. due to lack of data,
        // all contained arrays, dicts and variants (but not structs) will be empty, and
        // any values returned by read... will be garbage.
        // in any case, *isEmpty will be set to indicate whether the array is empty.

        void beginArray(bool *isEmpty = 0);
        // call this before reading each entry; when it returns false the array has ended.
        // TODO implement & document that all values returned by read... are zero/null?
        bool nextArrayEntry();
        void endArray(); // leaves the current array; only  call this in state EndArray!

        bool beginDict(bool *isEmpty = 0);
        bool nextDictEntry(); // like nextArrayEntry()
        bool endDict(); // like endArray()

        bool beginStruct();
        bool endStruct(); // like endArray()

        bool beginVariant();
        bool endVariant(); // like endArray()

        std::vector<CursorState> aggregateStack() const; // the aggregates the cursor is currently in

        // reading a type that is not indicated by state() will cause undefined behavior and at
        // least return garbage.
        byte readByte() { byte ret = m_Byte; advanceState(); return ret; }
        bool readBoolean() { bool ret = m_Boolean; advanceState(); return ret; }
        int16 readInt16() { int ret = m_Int16; advanceState(); return ret; }
        uint16 readUint16() { uint16 ret = m_Uint16; advanceState(); return ret; }
        int32 readInt32() { int32 ret = m_Int32; advanceState(); return ret; }
        uint32 readUint32() { uint32 ret = m_Uint32; advanceState(); return ret; }
        int64 readInt64() { int64 ret = m_Int64; advanceState(); return ret; }
        uint64 readUint64() { uint64 ret = m_Uint64; advanceState(); return ret; }
        double readDouble() { double ret = m_Double; advanceState(); return ret; }
        cstring readString() { cstring ret(m_String.begin, m_String.length); advanceState(); return ret; }
        cstring readObjectPath() { cstring ret(m_String.begin, m_String.length); advanceState(); return ret; }
        cstring readSignature() { cstring ret(m_String.begin, m_String.length); return ret; }
        uint32 readUnixFd() { uint32 ret = m_Uint32; advanceState(); return ret; }

    private:
        friend class ArgumentList;
        explicit ReadCursor(ArgumentList *al);
        CursorState doReadPrimitiveType();
        CursorState doReadString(int lengthPrefixSize);
        void advanceState();
        void advanceStateFrom(CursorState expectedState);
        void beginArrayOrDict(bool isDict, bool *isEmpty);
        bool nextArrayOrDictEntry(bool isDict);

        struct ArrayInfo
        {
            uint32 dataEnd; // one past the last data byte of the array
            uint32 containedTypeBegin; // to rewind when reading the next element
        };

        struct VariantInfo
        {
            podCstring prevSignature;       // a variant switches the currently parsed signature, so we
            uint32 prevSignaturePosition; // need to store the old signature and parse position.
        };

        // for structs, we don't need to know more than that we are in a struct

        struct AggregateInfo
        {
            CursorState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
            union {
                ArrayInfo arr;
                VariantInfo var;
            };
        };

        ArgumentList *m_argList;
        CursorState m_state;
        Nesting *m_nesting;
        cstring m_signature;
        array m_data;
        int m_signaturePosition;
        int m_dataPosition;
        int m_zeroLengthArrayNesting; // this keeps track of how many zero-length arrays we are in

        // this keeps track of which aggregates we are currently in
        std::vector<AggregateInfo> m_aggregateStack;

        // it is more efficient, in code size and performance, to read the data in advanceState()
        // and store the result for later retrieval in readFoo()
        union {
            byte m_Byte;
            bool m_Boolean;
            int16 m_Int16;
            uint16 m_Uint16;
            int32 m_Int32;
            uint32 m_Uint32;
            int64 m_Int64;
            uint64 m_Uint64;
            double m_Double;
            podCstring m_String; // also for ObjectPath and Signature
        };
    };

    // TODO: try to share code with ReadIterator
    class WriteCursor
    {
    public:
        ~WriteCursor();

        bool isValid() const { return m_argList; }

        CursorState state() const { return m_state; }
        cstring stateString() const;

        void beginArray(bool isEmpty);
        void nextArrayEntry();
        void endArray();

        void beginDict(bool isEmpty);
        void nextDictEntry();
        void endDict();

        void beginStruct();
        void endStruct();

        void beginVariant();
        void endVariant();

        void finish();

        std::vector<CursorState> aggregateStack() const; // the aggregates the cursor is currently in

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
        friend class ArgumentList;
        explicit WriteCursor(ArgumentList *al);

        CursorState doWritePrimitiveType(uint32 alignAndSize);
        CursorState doWriteString(int lengthPrefixSize);
        void advanceState(array signatureFragment, CursorState newState);
        void beginArrayOrDict(bool isDict, bool isEmpty);
        void nextArrayOrDictEntry(bool isDict);

        struct ArrayInfo
        {
            uint32 dataBegin; // one past the last data byte of the array
            uint32 containedTypeBegin; // to rewind when reading the next element
        };

        struct VariantInfo
        {
            podCstring prevSignature;       // a variant switches the currently parsed signature, so we
            uint32 prevSignaturePosition; // need to store the old signature and parse position.
            uint32 signatureIndex; // index in m_variantSignatures
        };

        struct StructInfo
        {
            uint32 containedTypeBegin;
        };

        struct AggregateInfo
        {
            CursorState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
            union {
                ArrayInfo arr;
                VariantInfo var;
                StructInfo sct;
            };
        };

        // we don't know how long a variant signature is when starting the variant, but we have to
        // insert the signature into the datastream before the data. for that reason, we need a
        // postprocessing pass to insert the signatures when the stream is otherwise finished.

        // - need to split up long string data: all unaligned, arbitrary length
        // - need to fix up array length because even the length excluding the padding before and
        //   after varies with alignment of the first byte - for that the alignment parameters as
        //   described in plan.txt need to be solved (we can use a worst-case estimate at first)
        struct ElementInfo
        {
            ElementInfo(byte alignment, byte size_)
               : size(size_)
            {
                assert(alignment <= 8);
                static const byte alignLog[9] = { 0, 0, 1, 0, 2, 0, 0, 0, 3 };
                alignmentExponent = alignLog[alignment];
                assert(alignment < 2 || alignLog != 0);
            }
            byte alignment() { return 1 << alignmentExponent; }

            uint32 alignmentExponent : 2; // powers of 2, so 1. 2. 4. 8
            uint32 size : 6; // that's up to 63
            enum SizeCode {
                LargestSize = 60,
                ArrayLengthField,
                ArrayLengthEndMark,
                VariantSignature
            };
        };

        std::vector<ElementInfo> m_elements;
        std::vector<cstring> m_variantSignatures; // TODO; cstring might not work when reallocating data

        ArgumentList *m_argList;
        CursorState m_state;
        Nesting *m_nesting;
        cstring m_signature;
        array m_data;
        int m_signaturePosition;
        int m_dataPosition;
        int m_zeroLengthArrayNesting;
        // this keeps track of which aggregates we are currently in
        std::vector<AggregateInfo> m_aggregateStack;

        union {
            byte m_Byte;
            bool m_Boolean;
            int16 m_Int16;
            uint16 m_Uint16;
            int32 m_Int32;
            uint32 m_Uint32;
            int64 m_Int64;
            uint64 m_Uint64;
            double m_Double;
            podCstring m_String; // also for ObjectPath and Signature
        };
    };

private:
    int m_isByteSwapped;
    int m_readCursorCount;
    bool m_hasWriteCursor;
    cstring m_signature;
    array m_data;
};
