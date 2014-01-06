/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#ifndef MESSAGE_H
#define MESSAGE_H

#include "argumentlist.h"
#include "iconnectionclient.h"
#include "types.h"

#include <map>
#include <string>
#include <vector>

class IConnection;
class ICompletionClient;

class Message : public IConnectionClient
{
public:
    // this class contains header data in deserialized form (maybe also serialized) and the payload
    // in serialized form

    Message(); // constructs an invalid message (to be filled in later, usually)

    std::string prettyPrint() const;

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
    uint32 protocolVersion() const;
    void setSerial(uint32 serial);
    uint32 serial() const;

    // more convenient access to headers
    void setPath(const std::string &path);
    void setInterface(const std::string &interface);
    void setMethod(const std::string &method);
    void setErrorName(const std::string &errorName);
    void setReplySerial(uint32 replySerial);
    void setDestination(const std::string &destination);
    void setSender(const std::string &sender);
    // you usually shouldn't need to call this; see setArgumentList()
    void setSignature(const std::string &signature);
    void setUnixFdCount(uint32 fdCount);

    std::string path() const;
    std::string interface() const;
    std::string method() const;
    std::string errorName() const;
    uint32 replySerial() const;
    std::string destination() const;
    std::string sender() const;
    std::string signature() const;
    uint32 unixFdCount() const;

    bool expectsReply() const;
    void setExpectsReply(bool);

    // "more generic", enum-based access to headers

    enum VariableHeader {
        PathHeader = 1,
        InterfaceHeader,
        MethodHeader, // called "member" in the spec
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

    // setArgumentList also sets the signature header of the message
    void setArgumentList(const ArgumentList &arguments);
    const ArgumentList &argumentList() const;

    void readFrom(IConnection *connection); // fills in this message from connection
    bool isReading() const;
    void writeTo(IConnection *connection); // sends this message over connection
    bool isWriting() const;

    // for read or write completion (it should be clear which because reading and writing can't
    // happen simultaneously)
    void setCompletionClient(ICompletionClient *client);

    std::vector<byte> save();
    void load(const std::vector<byte> &data);

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

    void notifyCompletionClient();

    // there is no explicit dirty flag; the buffer is simply cleared when dirtying any of the data below.
    std::vector<byte> m_buffer;

    bool m_isByteSwapped;
    enum {
        Empty,
        Serialized,
        Deserialized,
        LastSteadyState = Deserialized,
        Serializing,
        Deserializing
    } m_state;
    Type m_messageType;
    enum {
        NoReplyExpectedFlag = 1,
        NoAutoStartFlag = 2
    };
    byte m_flags;
    byte m_protocolVersion;
    uint32 m_headerLength;
    uint32 m_headerPadding;
    uint32 m_bodyLength;
    uint32 m_serial;

    ArgumentList m_mainArguments;

    std::map<int, std::string> m_stringHeaders;
    std::map<int, uint32> m_intHeaders;

    ICompletionClient *m_completionClient;
};

#endif // MESSAGE_H
