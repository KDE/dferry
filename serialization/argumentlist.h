#include "types.h"

#include <vector>

class Nesting; // TODO remove this when we've d-pointerized everything

class ArgumentList
{
public:
    ArgumentList(); // constructs an empty argument list
     // constructs an argument list to deserialize data in @p data with signature @p signature
    ArgumentList(array signature, array data, bool isByteSwapped);

    // valid when no write cursor is open on the instance
    int length() const;

     // returns true when at least one read cursor is open, false otherwise
    bool isReading() const { return m_readCursorCount; }
    // returns true when a write cursor is open, false otherwise
    bool isWriting() const { return m_writeCursor; }

    class ReadCursor;
    class WriteCursor;
    ReadCursor beginRead() { return ReadCursor(m_writeCursor ? 0 : this); }
    WriteCursor beginWrite() { return WriteCursor((m_writeCursor || m_readCursorCount) ? 0 : this); }

    static bool isSignatureValid(array signature, bool requireSingleCompleteType = false);

    enum CursorState {
        // "exceptional" states
        NotStarted = 0,
        Finished,
        NeedMoreData, // recoverable by adding data; should only happen when parsing the not length-prefixed variable message header
        InvalidData, // non-recoverable
        AnyData, // occurs in WriteCursor when you are free to add any type

        // the following occur in ReadCursor, and in WriteCursor when in the second or higher iteration
        // of an array or dict where the types must match the first iteration (except inside variants).

        // states pertaining to aggregates
        BeginArray,
        NextArrayEntry,
        EndArray, // TODO do we really need this and endArray()? same for EndDict.
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

    // a cursor is similar to an iterator, but more tied to the underlying data structure
    // error handling is done by asking state() or isError(), not by method return values.
    // occasionally looking at isError() is less work than checking every call.
    class ReadCursor
    {
    public:
        ~ReadCursor();

        bool isValid() const { return m_argList; }

        CursorState state() const { return m_state; }
         // HACK call this in NeedMoreData state when more data has been added; this replaces m_data
         // ### will need to fix up any VariantInfo::prevSignature on the stack where prevSignature
         //     is inside m_data; length will still work but begin will be outdated.
        void replaceData(array data);

        bool isFinished() const { return m_state == Finished; }
        bool isError() const { return m_state == InvalidData || m_state == NeedMoreData; }

        void beginArray();
        bool nextArrayEntry(); // call this before reading each entry; when it returns false the array has ended
        void endArray(); // leaves the current array; only  call this in state EndArray!

        bool beginDict();
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
        double readDouble() { byte ret = m_Double; advanceState(); return ret; }
        // ### note that the terminating nul is counted in array.length!
        array readString() { array ret(m_String.begin, m_String.length); advanceState(); return ret; }
        array readObjectPath() { array ret(m_String.begin, m_String.length); advanceState(); return ret; }
        array readSignature() { array ret(m_String.begin, m_String.length); return ret; }
        uint32 readUnixFd() { uint32 ret = m_Uint32; advanceState(); return ret; }

    private:
        friend class ArgumentList;
        ReadCursor(ArgumentList *al);
        void advanceState();

        ArgumentList *m_argList;
        CursorState m_state;
        Nesting *m_nesting;
        array m_signature;
        array m_data;
        int m_signaturePosition;
        int m_dataPosition;

        struct podArray // can't put the array type into a union because it has a constructor :/
        {
            byte *begin;
            uint32 length;
        };

        struct ArrayInfo
        {
            uint32 dataEndPosition; // one past the last data byte of the array
            uint32 signatureContainedTypePosition; // to rewind when reading the next element
        };

        struct VariantInfo
        {
            podArray prevSignature;       // a variant switches the currently parsed signature, so we
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
        // to go back to the beginning of e.g. an array signature before fetching the next element
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
            podArray m_String; // also for ObjectPath and Signature
        };
    };

    class WriteCursor
    {
        ~WriteCursor();

        bool isValid() const { return m_argList; }

        CursorState state() const;

        void beginArray();
        void nextArrayEntry();
        void endArray();

        void beginDict();
        void nextDictEntry();
        void endDict();

        void beginStruct();
        void endStruct();

        void beginVariant();
        void endVariant();

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
        void writeString(array a);
        void writeSignature(array a);

    private:
        friend class ArgumentList;
        WriteCursor(ArgumentList *al);

        ArgumentList *m_argList;
        int m_signaturePosition;
        int m_dataPosition;
        static const int maxNesting = 64; // this is in the standard
        // to go back to the beginning of e.g. an array signature before fetching the next element
        int m_signaturePositionStack[maxNesting];
    };

private:
    // friend class ReadCursor; // TODO required?
    // friend class WriteCursor;
    int m_isByteSwapped;
    int m_readCursorCount;
    WriteCursor *m_writeCursor;
    array m_signature;
    array m_data;
};
