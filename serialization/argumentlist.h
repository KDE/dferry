#include "types.h"

#include <vector>

class Nesting; // TODO remove this when we've d-pointerized everything

class ArgumentList
{
public:
    enum Type {
        // using all-caps in order to match the d-bus spec. this also distinguishes spec-mandated
        // enums / enum values from implementation-specific ones.
        INVALID = 0,
        BYTE = 121, // y
        BOOLEAN = 98, // b
        INT16 = 110, // n
        UINT16 = 113, // q
        INT32 = 105, // i
        UINT32 = 117, // u
        INT64 = 120, // x
        UINT64 = 116, // t
        DOUBLE = 100, // d
        STRING = 115, // s
        ARRAY = 97, // a
        STRUCT = 114, // r
        VARIANT = 118, // v
        DICT_ENTRY = 101, // e
        UNIX_FD = 104 // h
    };

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

    static bool isSignatureValid(array signature);

    // a cursor is similar to an iterator, but more tied to the underlying data structure
    // error handling is done by asking state() or isError(), not by method return values.
    // occasionally looking at isError() is less work than checking every call.
    class ReadCursor
    {
    public:
        ~ReadCursor();

        bool isValid() const { return m_argList; }

        enum State {
            // "exceptional" states
            NotStarted = 0,
            Finished,
            NeedMoreData, // recoverable by adding data; should only happen when parsing the not length-prefixed variable message header
            InvalidData, // non-recoverable
            // states pertaining to aggregates
            BeginArray,
            NextArrayElement,
            EndArray,
            BeginDict,
            NextDictElement,
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

        State state() const { return m_state; }
        replaceData(array data); // HACK call this in NeedMoreData state when more data has been added; this replaces m_data

        bool isFinished() const { return m_state == Finished; }
        bool isError() const { return m_state == InvalidData || m_state == NeedMoreData; }

        void beginArray(int *size);
        void endArray(); // leaves the current array even when not finished reading it

        bool beginDict(int *size);
        bool endDict(); // like endArray()

        bool beginStruct();
        bool endStruct(); // like endArray()

        bool beginVariant();
        bool endVariant(); // like endArray()

        std::vector<Type> aggregateStack() const; // the aggregates the cursor is currently in

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
        array readString() { array ret = m_String; advanceState(); return ret; }
        array readObjectPath() { array ret = m_String; advanceState(); return ret; }
        array readSignature() { array ret = m_String; advanceState(); return ret; }
        uint32 readUnixFd() { uint32 ret = m_Uint32; advanceState(); return ret; }

    private:
        friend class ArgumentList;
        ReadCursor(ArgumentList *al);
        void advanceState();

        ArgumentList *m_argList;
        State m_state;
        Nesting *m_nesting;
        array m_signature;
        array m_data;
        int m_signaturePosition;
        int m_dataPosition;
        static const int maxNesting = 64; // this is in the standard
        // to go back to the beginning of e.g. an array signature before fetching the next element
        int m_signaturePositionStack[maxNesting];

        // it is more efficient to read the data in one big method and store the result for
        // retrieval in readFoo()
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
            array m_string; // also for ObjectPath and Signature
        }
    };

    class WriteCursor
    {
        ~WriteCursor();

        bool isValid() const { return m_argList; }

        enum State {
            NotStarted = 0,
            Toplevel, // in this state writing can be stopped and the result will be a valid message
            InAggregate, // in this state at least one aggregate has been begun and not finished
            RepeatedElementTypeMismatch, // when trying to add different types to an array; the first element dictates the type.
        };

        State state() const;

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

        std::vector<Type> aggregateStack() const; // the aggregates the cursor is currently in

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
