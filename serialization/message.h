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

#include "types.h"

#include <string>
#include <vector>

class ArgumentList;
class IConnection;
class ICompletionClient;
class MessagePrivate;

// TODO: some separation between convenience and low-level API, move all convenience API to top

class DFERRY_EXPORT Message
{
public:
    // this class contains header data in deserialized form (maybe also serialized) and the payload
    // in serialized form

    Message(); // constructs an invalid message (to be filled in later, usually)
    ~Message();

    Message(Message &&other);
    Message &operator=(Message &&other);

    // might need to implement them later
    Message(const Message &other) = delete;
    Message &operator=(const Message &other) = delete;

    // convenience
    void setCall(const std::string &path, const std::string &interface, const std::string &method);
    void setCall(const std::string &path, const std::string &method); // deprecated? remove?
    void setReplyTo(const Message &call); // fills in all available details as appropriate for a reply
    void setErrorReplyTo(const Message &call, const std::string &errorName);
    void setSignal(const std::string &path, const std::string &interface, const std::string &method);
    // decadence
    static Message createCall(const std::string &path, const std::string &interface,
                              const std::string &method);
    static Message createCall(const std::string &path, const std::string &method);
    static Message createReplyTo(const Message &call);
    static Message createErrorReplyTo(const Message &call, const std::string &errorName);
    static Message createSignal(const std::string &path, const std::string &interface,
                                const std::string &method);

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

    // These are validated during serialization, not now; the message cannot expected to be in a
    // completely valid state before that anyway. Yes, we could validate some things, but let's just
    // do it all at once.
    std::string stringHeader(VariableHeader header, bool *isPresent = 0) const;
    void setStringHeader(VariableHeader header, const std::string &value);
    uint32 intHeader(VariableHeader header, bool *isPresent = 0) const;
    void setIntHeader(VariableHeader header, uint32 value);

    // TODO a method that returns if the message is valid in its current state (flags have valid
    //      values, mandatory variable header fields for the message type are present, ...?

    // setArgumentList also sets the signature header of the message
    void setArgumentList(ArgumentList arguments);
    const ArgumentList &argumentList() const;

    std::vector<byte> save();
    void load(const std::vector<byte> &data);

    // The rest of public methods is low-level API that should only be used in very special situations

    void setSerial(uint32 serial);
    uint32 serial() const;

    void receive(IConnection *connection); // fills in this message from connection
    bool isReceiving() const;
    void send(IConnection *connection); // sends this message over connection
    bool isSending() const;

    // for read or write completion (it should be clear which because reading and writing can't
    // happen simultaneously)
    // ### might want to remove it: this is a value class, the completion client is kind of an identity aspect
    //     because what would the completion client of a copy be? same pointer or null? both are bad. see std::auto_ptr.
    void setCompletionClient(ICompletionClient *client);

private:
    MessagePrivate *d;
};

#endif // MESSAGE_H
