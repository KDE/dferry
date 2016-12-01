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
    Arguments::Writer -> Arguments -> Message -> PendingReply

*/

#ifndef ERROR_H
#define ERROR_H

#include "types.h"

#include <string>

class DFERRY_EXPORT Error {
public:
    enum Code : uint32 {
        // Error error ;)
        NoError = 0,

        // Arguments errors
        NotAttachedToArguments,
        InvalidSignature,
        ReplacementDataIsShorter,
        MalformedMessageData,
        ReadWrongType,
        InvalidType,
        InvalidString,
        InvalidObjectPath,
        SignatureTooLong,
        ExcessiveNesting,
        CannotEndArgumentsHere,
        ArgumentsTooLong,

        NotSingleCompleteTypeInVariant,
        EmptyVariant,
        CannotEndVariantHere,

        EmptyStruct,
        CannotEndStructHere,

        NotSingleCompleteTypeInArray,
        TypeMismatchInSubsequentArrayIteration,
        CannotEndArrayHere,
        CannotEndArrayOrDictHere,
        TooFewTypesInArrayOrDict,
        InvalidStateToRestartEmptyArray,
        InvalidKeyTypeInDict,
        GreaterTwoTypesInDict,
        ArrayOrDictTooLong,

        MissingBeginDictEntry = 1019,
        MisplacedBeginDictEntry,
        MissingEndDictEntry,
        MisplacedEndDictEntry,
        // we have a lot of error codes at our disposal, so reserve some for easy classification
        // by range
        MaxArgumentsError = 1023,
        // end Arguments errors

        // Message  / PendingReply
        DetachedPendingReply,
        Timeout,
        Connection,
        LocalDisconnect,
        MalformedReply, // Catch-all for failed reply validation - can't be corrected locally anyway.
                        // Since the reply isn't fully pre-validated for performance reasons,
                        // absence of this error is no guarantee of well-formedness.

        MessageType,        // ||| all of these may potentially mean missing for the type of message
        MessageSender,      // vvv or locally found to be invalid (invalid object path for example)
        MessageDestination,
        MessagePath,
        MessageInterface,
        MessageSignature,
        MessageMethod,
        MessageErrorName,
        MessageSerial,
        MessageReplySerial,
        MessageProtocolVersion,


        PeerNoSuchReceiver,
        PeerNoSuchPath,
        PeerNoSuchInterface,
        PeerNoSuchMethod,

        ArgumentTypeMismatch,
        PeerInvalidProperty,
        PeerNoSuchProperty,
        AccessDenied, // for now(?) only properties: writing to read-only / reading from write-only
        MaxMessageError = 2047
        // end Message / PendingReply errors


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
