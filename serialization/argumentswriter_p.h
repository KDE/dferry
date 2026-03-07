// This is inside the class definition for Arguments, hence no header guard and "private:" and "public:",
// but otherwise it's just like a class definition

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
