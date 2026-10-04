#include "error.h"

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
std::string Error::message() const
{
    // TODO looking up a yet to be written string from error code goes here
    return std::string();
}
