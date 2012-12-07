#ifndef MESSAGE_H
#define MESSAGE_H

#include "argumentlist.h"
#include "iconnectionclient.h"
#include "types.h"

#include <map>
#include <string>
#include <vector>

class IConnection;

class Message : public IConnectionClient
{
public:
    // this class contains header data in deserialized form (maybe also serialized) and the payload
    // in serialized form

    Message(int serial); // constructs a new message (to be serialized later, usually)

    Message(); // constructs an invalid message (to be filled in later, usually)

    enum Type {
        InvalidMessage = 0,
        MethodCallMessage,
        MethodReturnMessage,
        ErrorMessage,
        SignalMessage
    };

    // TODO try to enforce sanity via checks and a restrictive API
    Type type() const;
    void setType(Type type);
    byte flags() const;
    uint32 protocolVersion() const;
    int serial() const;

    enum VariableHeader {
        PathHeader = 1,
        InterfaceHeader,
        MemberHeader,
        ErrorNameHeader,
        ReplySerialHeader,
        DestinationHeader,
        SenderHeader,
        SignatureHeader,
        UnixFdsHeader
    };

    std::string stringHeader(VariableHeader header, bool *isPresent = 0) const;
    bool setStringHeader(VariableHeader header, const std::string &value);
    uint32 intHeader(VariableHeader header, bool *isPresent = 0) const;
    bool setIntHeader(VariableHeader header, uint32 value);

    // TODO a method that returns if the message is valid in its current state (flags have valid
    //      values, mandatory variable header fields for the message type are present, ...?

    void setArgumentList(const ArgumentList &arguments);
    const ArgumentList &argumentList() const;

    void readFrom(IConnection *connection); // fills in this message from connection
    bool isReading() const;
    void writeTo(IConnection *connection); // sends this message over connection
    bool isWriting() const;

protected:
    virtual void notifyConnectionReadyRead();
    virtual void notifyConnectionReadyWrite();

private:
    bool requiredHeadersPresent() const;
    bool deserializeFixedHeaders();
    bool deserializeVariableHeaders();
    bool fillOutBuffer();
    void serializeFixedHeaders();
    void serializeVariableHeaders(ArgumentList *headerArgs);

    // there is no explicit dirty flag; the buffer is simply cleared when dirtying any of the data below.
    std::vector<byte> m_buffer;

    bool m_isByteSwapped;
    enum {
        NoIo,
        ReadIo,
        WriteIo
    } m_io;
    Type m_messageType;
    byte m_flags;
    byte m_protocolVersion;
    uint32 m_headerLength;
    uint32 m_headerPadding;
    uint32 m_bodyLength;
    uint32 m_serial;

    ArgumentList m_mainArguments;

    std::map<int, std::string> m_stringHeaders;
    std::map<int, uint32> m_intHeaders;
};

#endif // MESSAGE_H
