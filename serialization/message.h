#ifndef MESSAGE_H
#define MESSAGE_H

#include "types.h"

#include <map>

class ArgumentList;

class Message
{
    // -- the following methods must only be used when serializing a message --

    Message(int serial); // constructs a new message
    // TODO methods to set the type, flags etc., try to enforce sanity via checks and a restrictive API
    // TODO void setArgumentList(const ArgumentList &arguments)


    // -- the following methods must only be used when DEserializing a message --

    // deserializes a message from a data stream; a must be large enough to contain the fixed headers,
    // i.e. 12 bytes.
    Message(array a);

    // add more data; there are two use cases for this:
    // - when forwarding a message, do it until isHeaderComplete(), then pass on the rest of the data unparsed
    // - when preparing a message for the final consumer, read until missingBytesCountForBody() == 0.
    void addData(array a);
    // returns false as long as more data is required to get all the variable headers.
    bool isHeaderComplete() const { return !m_headerParser; }

    // When forwarding a message, only the header is required for routing. The rest of the data can
    // be passed on without looking at it. This tells how much such data should be passed on.
    // When the header is not yet complete, this returns -1.
    int32 missingBytesCountForBody() const;

private:
    void parseVariableHeaders();

    array m_data;

    bool m_isByteSwapped;
    byte m_messageType;
    byte m_flags;
    byte m_protocolVersion;
    uint32 m_bodyLength;
    uint32 m_serial;

    ArgumentList *m_headerParser;
    std::map<byte, cstring> m_stringHeaders;
    std::map<byte, uint32> m_intHeaders;
};

#endif // MESSAGE_H
