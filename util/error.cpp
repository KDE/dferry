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

std::string Error::message() const
{
    // TODO looking up a yet to be written string from error code goes here
    return std::string();
}
