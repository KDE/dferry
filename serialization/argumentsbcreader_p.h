// This is inside the class definition for Arguments, hence no header guard and "private:" and "public:",
// but otherwise it's just like a class definition


public:
// error handling is done by asking state() or isError(), not by method return values.
// occasionally looking at isError() is less work than checking every call.
class DFERRY_EXPORT BcReader
{
public:
    explicit BcReader(const Arguments &al);
    explicit BcReader(const Message &msg);
    BcReader(BcReader &&other);
    void operator=(BcReader &&other);
    // TODO unit-test copy and assignment
    BcReader(const BcReader &other);
    void operator=(const BcReader &other);

    ~BcReader();

    bool isValid() const;
    Error error() const; // see also: aggregateStack()

    IoState state() const { return m_state; }
    //cstring stateString() const;
    //bool isInsideEmptyArray() const;
    //cstring currentSignature() const; // current signature, either main signature or current variant
    //uint32 currentSignaturePosition() const;
    //cstring currentSingleCompleteTypeSignature() const;

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
    //void skipArray(); // skips the current array; only  call this in state BeginArray!
    void endArray(); // leaves the current array; only  call this in state EndArray!

    bool beginDict(EmptyArrayOption option = SkipIfEmpty);
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
    byte readByte()     { return *reinterpret_cast<const byte *  >(advanceState()); }
    // Yes, booleans are stored as a uint32 with possible values 0 and 1 in DBus!
    bool readBoolean()  { return *reinterpret_cast<const uint32 *>(advanceState()); }
    int16 readInt16()   { return *reinterpret_cast<const int16 * >(advanceState()); }
    uint16 readUint16() { return *reinterpret_cast<const uint16 *>(advanceState()); }
    int32 readInt32()   { return *reinterpret_cast<const int32 * >(advanceState()); }
    uint32 readUint32() { return *reinterpret_cast<const uint32 *>(advanceState()); }
    int64 readInt64()   { return *reinterpret_cast<const int64 * >(advanceState()); }
    uint64 readUint64() { return *reinterpret_cast<const uint64 *>(advanceState()); }
    double readDouble() { return *reinterpret_cast<const double *>(advanceState()); }
    cstring readString()
    {
        const void* retPtr = advanceState();
        return cstring(reinterpret_cast<const char *>(retPtr) + sizeof(uint32),
                       *reinterpret_cast<const uint32 *>(retPtr));
    }
    cstring readObjectPath()
    {
        const void* retPtr = advanceState();
        return cstring(reinterpret_cast<const char *>(retPtr) + sizeof(byte),
                       *reinterpret_cast<const byte *>(retPtr));
    }
    cstring readSignature() { return readObjectPath(); }
    int32 readUnixFd() { return readInt32(); }  // TODO I think it doesn't work like that, needs a lookup
                                                // into the table of received file descriptors

    //void skipCurrentElement(); // works on single values and Begin... states. In the Begin... states,
                                // skips the whole aggregate.

    // Returns primitive type and the raw array data if in BeginArray state of an array containing only a
    // primitive type. You must copy the data before destroying the BcReader or changing its backing store
    // with replaceData().
    // If the array is empty, that does not constitute a special case with this function: It will return
    // the type in the first return value as usual and an empty chunk in the second return value.
    // (### it might be possible to extend this feature to all fixed-length types including structs)
    //std::pair<Arguments::IoState, chunk> readPrimitiveArray();
    // In state BeginArray, check if the array is a primitive array, in order to check whether to use
    // readPrimitiveArray(). Returns a primitive type if readPrimitiveArray() will succeed, BeginArray
    // if the array is not primitive, InvalidData if state is not BeginArray. The latter will not put
    // the reader in InvalidData state.
    // If option is SkipIfEmpty, an empty array of primitives will result in a return value of BeginArray
    // instead of the type of primitive.
    //Arguments::IoState peekPrimitiveArray(EmptyArrayOption option = SkipIfEmpty) const;

#ifdef WITH_DICT_ENTRY
    void beginDictEntry();
    void endDictEntry();
#endif

private:
    void beginRead();
    //void doReadPrimitiveType();
    void doReadString(uint32 lengthPrefixSize);
    bool beginArrayInternal(EmptyArrayOption option);
    const void *advanceState();
    //void beginArrayOrDict(bool isDict, EmptyArrayOption option);
    //void skipArrayOrDictSignature(bool isDict);
    //void skipArrayOrDict(bool isDict);

    IoState m_state;

    class Private;
    friend class Private;
    Private *d;
};
