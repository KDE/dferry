/*
    Design notes about errors.

    Errors can come (including but not limited to...) from these areas:
    - Arguments assembly
      - invalid construct, e.g. empty struct, dict with key but no value, dict with invalid key type,
        writing different (non-variant) types in different array elements
      - limit exceeded (message size, nesting depth etc)
      - invalid single data (e.g. null in string, too long string)
    - Arguments disassembly
      - malformed data (mostly manifesting as limit exceeded, since the format has little room for
        "grammar errors" - almost everything could theoretically be valid data)
      - invalid single data
      - trying to read something incompatible with reader state
    - Message assembly
      - required headers not present
    - Message disassembly
      - required headers not present (note: sender header in bus connections! not currently checked.)
    - I/O errors
      - could not open connection - any sub-codes?
      - disconnected
      - timeout??
      - (read a malformed message - connection should be closed)
      - discrepancy in number of file descriptors advertised and actually received - when this
        is implemented
    - artifacts of the implementation; not much - using a default-constructed PendingReply, anything else?
    - error codes from standardized DBus interfaces like the introspection thing; I think the convenience
      stuff should really be separate! Maybe separate namespace, in any case separate enum

    an error (if any) propagates in the following way, so you don't need to check at every step:
    ArgumentsWriter -> Arguments -> Message -> PendingReply

*/

#ifndef ERROR_H
#define ERROR_H

#include "types.h"

#include <string>

class DFERRY_EXPORT Error {
public:
    /// Error codes
    enum Code : uint32 {
        NoError = 0, ///< No error

        // Arguments errors
        NotAttachedToArguments, ///< ArgumentsReader not attached to Arguments instance
        InvalidSignature, ///< Type signature is invalid (error when reading Arguments)
        TruncatedMessageData, ///< Serialized data is truncated (too short)
        MalformedMessageData, ///< Serialized data is malformed
        ReadWrongType, ///< Tried to read a type that is not the actual current type
        InvalidString, ///< String is invalid (e.g. invalid UTF-8 or embedded null bytes)
        InvalidObjectPath, ///< DBus object path is invalid
        SignatureTooLong, ///< Type signature is too long (error when writing Arguments)
        ExcessiveNesting, ///< Arguments nested too deeply
        CannotEndArgumentsWithOpenAggregates, ///< Tried to end writing arguments with unclosed aggregate(s)
        ArgumentsTooLong, ///< Arguments too long to fit into a Message (max 128 MiB)

        NotDirectlyInVariant, ///< Variant is not the current ("top of stack") aggregate
        NotSingleCompleteTypeInVariant, ///< Type in variant is not a single complete type

        NotDirectlyInStruct, ///< Struct is not the current ("top of stack") aggregate
        EmptyStruct, ///< Struct does not contain any types

        NotDirectlyInArray, ///< Array is not the current ("top of stack") aggregate
        NotSingleCompleteTypeInArray, ///< Type in array is not a single complete type

        NotPrimitiveType, ///< Writing array of primitives: element type is not a primitive type
        DataLengthNotMultipleOfElementLength, ///< Writing array of primitives: input data length is not an
                                              ///  integer multiple of element length
        InvalidStateToRestartEmptyArray, // TODO? remove or make it a Qt special

        NotDirectlyInDict, ///< Dict is not the current ("top of stack") aggregate
        DictKeyNotBasicType, ///< Dict key is not a numeric, boolean or string type
        NotKeyAndValueTypesInDict, ///< Dict does not contain exactly two types: key and value

        TypeMismatchInArrayOrDictRepetition, ///< On a pass through an array or dict after the first, tried
                                             ///  to write a different type than in the first pass which
                                             ///  defined the type.
        ArrayOrDictTooLong, ///< Array or dict too long (max 64 MiB)

        StateNotSkippable, ///< Tried to skip an argument type that cannot be skipped
#ifdef WITH_DICT_ENTRY
        MissingBeginDictEntry = 1019,
        MisplacedBeginDictEntry,
        MissingEndDictEntry,
        MisplacedEndDictEntry,
#endif
        // we have a lot of error codes at our disposal, so reserve some for easy classification
        // by range
        MaxArgumentsError = 1023,
        // end Arguments errors

        // Message  / PendingReply
        DetachedPendingReply, ///< PendingReply is default-constructed or has its result already taken out
        PendingReplyNotFinished, ///< PendingReply has not finished yet
        Timeout, ///< No reply received before timeout
        MalformedReply, ///< Received a reply that is somehow invalid (bad encoding, missing fields etc).
                        ///  Absence of this error does not guarantee that the reply is fully valid -
                        ///  in particular, its Arguments (if any) are only validated while deserializing
                        ///  them.

        // ||| all of these may potentially mean missing for the type of message
        // vvv or locally found to be invalid (invalid object path for example)
        MessageType, ///< Message type is unsuitable
        MessageSender, ///< Message sender is invalid
        MessageDestination, ///< Message destination is invalid
        MessagePath, ///< Message (DBus object) path is invalid
        MessageInterface, ///< Message interface is invalid
        MessageSignature, ///< Message (DBus type) signature is invalid
        MessageMethod, ///< Message method name is invalid
        MessageErrorNameMissing, ///< DBus error-type Message is missing error name
        MessageSerial, ///< Message serial number is invalid
        MessageReplySerial, ///< Message reply serial number is invalid
        MessageProtocolVersion, ///< Message protocol version is not supported

#if 0
        PeerNoSuchReceiver,
        PeerNoSuchPath,
        PeerNoSuchInterface,
        PeerNoSuchMethod,

        ArgumentTypeMismatch,
        PeerInvalidProperty,
        PeerNoSuchProperty,
        AccessDenied, // for now(?) only properties: writing to read-only / reading from write-only
#endif
        MaxMessageError = 2047,
        // end Message / PendingReply errors

        // Connection
        AuthenticationFailed, ///< Could not authenticate to message bus
        RemoteDisconnect, ///< Remote side disconnected
        LocalDisconnect, ///< Local side disconnected or never connected
        SendingTooManyUnixFds, ///< Tried to send a message containing more file descriptors
                               ///  than the connection supports.
                               ///  \see Connection::supportedFileDescriptorsPerMessage()
        MaxConnectionError = 3071,

        // errors for other occasions go here
    };

    Error() : m_code(NoError) {}
    Error(Code code) : m_code(code) {}
    void setCode(Code code) { m_code = code; }
    Code code() const { return m_code; }
    bool isError() const { return m_code != NoError; }
    // no setter for message - it is just looked up from a static table according to error code
    std::string message() const;

private:
    Code m_code;
};

#endif // ERROR_H
