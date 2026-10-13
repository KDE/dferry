#include "error.h"

#include <iterator> // std::size()

/** \class Error
    For all errors in Dferry.

    Dferry extensively uses the concept of error chaining: In a multi-step operation such as
    constructing a message, sending it, and receiving a reply, an error result from an earlier
    step can safely be used in later steps, resulting in an error result. In many common
    scenarios, it is not required to check for errors at every step; the first error that
    occurred will still be the error seen at the end of the chain.
    There is just one Error class containing all possible error codes in Dferry in order to
    facilitate error chaining.
*/

/// Creates an Error with error code NoError.
/// \fn Error::Error()

/// Creates an Error with error code \p code.
/// \fn Error::Error(Code code)

/// Sets this Error's error code
/// \fn void Error::setCode(Code code)

/// \returns this Error's error code
/// \fn Code Error::code() const { return m_code; }

/// \fn bool Error::isError() const
/// \returns whether this Error's error code signifies an actual error (i.e. it's not NoError)

/// \returns a string representation of this Error's error code.
const char *Error::string() const
{
    static const char* const names[] = {
        "NoError",
        // Arguments errors
        "NotAttachedToArguments",
        "InvalidSignature",
        "TruncatedMessageData",
        "MalformedMessageData",
        "ReadWrongType",
        "InvalidString",
        "InvalidObjectPath",
        "SignatureTooLong",
        "ExcessiveNesting",
        "CannotEndArgumentsWithOpenAggregates",
        "ArgumentsTooLong",
        "DataNot8ByteAligned",
        "NotDirectlyInVariant",
        "NotSingleCompleteTypeInVariant",
        "NotDirectlyInStruct",
        "EmptyStruct",
        "NotDirectlyInArray",
        "NotSingleCompleteTypeInArray",
        "NotPrimitiveType",
        "DataLengthNotMultipleOfElementLength",
        "InvalidStateToRestartEmptyArray",
        "NotDirectlyInDict",
        "DictKeyNotBasicType",
        "NotKeyAndValueTypesInDict",
        "TypeMismatchInArrayOrDictRepetition",
        "ArrayOrDictTooLong", ///< Array or dict too long (max 64 MiB)
        "StateNotSkippable", ///< Tried to skip an argument type that cannot be skipped
#ifdef WITH_DICT_ENTRY
        "MissingBeginDictEntry = 1019",
        "MisplacedBeginDictEntry",
        "MissingEndDictEntry",
        "MisplacedEndDictEntry",
#endif
        "MaxArgumentsError", // = 1023

        // Message  / PendingReply
        "DetachedPendingReply",
        "PendingReplyNotFinished",
        "Timeout",
        "MalformedReply",
        "MessageType",
        "MessageSender",
        "MessageDestination",
        "MessagePath",
        "MessageInterface",
        "MessageSignature",
        "MessageMethod",
        "MessageErrorNameMissing",
        "MessageSerial",
        "MessageReplySerial",
        "MessageProtocolVersion",
#if 0
        "PeerNoSuchReceiver",
        "PeerNoSuchPath",
        "PeerNoSuchInterface",
        "PeerNoSuchMethod",

        "ArgumentTypeMismatch",
        "PeerInvalidProperty",
        "PeerNoSuchProperty",
        "AccessDenied",
#endif
        "MaxMessageError", // = 2047
        // Connection
        "AuthenticationFailed",
        "RemoteDisconnect",
        "LocalDisconnect",
        "SendingTooManyUnixFds",
        "MaxConnectionError", // = 3071
    };

    struct Group {
        constexpr uint32 count() const { return last - first; }
        uint32 first;               uint32 last;
    };
    static constexpr Group groups[] = {
        {NoError,                   LastArgumentsError},
        {MaxArgumentsError,         LastMessageError},
        {MaxMessageError,           LastConnectionError},
        {MaxConnectionError,        LastOtherError},
        {MaxConnectionError + 1,    MaxConnectionError + 1}, // sentinel item, only .first is used
    };
    static_assert(groups[0].count() + groups[1].count() + groups[2].count() + groups[3].count()
                  == std::size(names),
                  "Group counts don't add up to the names table size");

    uint32 base = 0;
    for (uint32 i = 0; i < std::size(groups) - 1; i++) {
        if (m_code < groups[i + 1].first) {
            const uint32 indexInGroup = m_code - groups[i].first;
            const uint32 tableIndex = base + indexInGroup;
            if (m_code >= groups[i].last || tableIndex >= std::size(names)) {
                break; // error: invalid enum value or our group data is wrong
            }
            return names[tableIndex];
        }
        base += groups[i].count();
    }

    return "<unknown error code>";
}
