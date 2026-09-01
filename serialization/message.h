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

#include "arguments.h"
#include "types.h"

#include <string>
#include <vector>

class Arguments;
class Error;
class MessagePrivate;

class DFERRY_EXPORT Message
{
public:
    // this class contains header data in deserialized form (maybe also serialized) and the payload
    // in serialized form

    Message(); // constructs an invalid message (to be filled in later, usually)
    ~Message();

    // prefer these over copy construction / assignment whenever possible, for performance reasons
    Message(Message &&other);
    Message &operator=(Message &&other);

    Message(const Message &other);
    Message &operator=(const Message &other);

    // error (if any) propagates to PendingReply, so it is still available later
    Error error() const;

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

    // TODO document message type -> required variable headers
    /// DBus message type
    enum Type {
        InvalidMessage = 0, ///< Invalid message
        MethodCallMessage, ///< Method call message
        MethodReplyMessage, ///< Method reply message (in response to a method call)
        ErrorMessage, ///< Error message (usually in response to a method call)
        SignalMessage  ///< DBus signal message
    };

    Type type() const;
    void setType(Type type);
    uint32 protocolVersion() const;

    /// Type of optional message header. Some of these are mandatory for certain message types.
    enum VariableHeader {
        PathHeader = 1, ///< Object path
        InterfaceHeader, ///< Interface name
        MethodHeader, ///< Interface name
        ErrorNameHeader, ///< The name of the error, for error messages (type ErrorMessage)
        ReplySerialHeader, ///< Replies only: Serial number of the corresponding call. \sa Message::serial()
        DestinationHeader, ///< Bus name of destination
        SenderHeader, ///< Bus name of sender
        SignatureHeader, ///< Type signature of message arguments
        UnixFdsHeader ///< Number of Unix file descriptors attached to the message
    };

    // enum-based access to headers
    // These are validated during serialization, not now; the message cannot expected to be in a
    // completely valid state before that anyway. Yes, we could validate some things, but let's just
    // do it all at once.
    std::string stringHeader(VariableHeader header, bool *isPresent = nullptr) const;
    void setStringHeader(VariableHeader header, const std::string &value);
    uint32 intHeader(VariableHeader header, bool *isPresent = nullptr) const;
    void setIntHeader(VariableHeader header, uint32 value);

    // convenience access to headers, these directly call enum-based getters / setters
    std::string path() const;
    void setPath(const std::string &path);
    std::string interface() const;
    void setInterface(const std::string &interface);
    std::string method() const;
    void setMethod(const std::string &method);
    std::string errorName() const;
    void setErrorName(const std::string &errorName);
    uint32 replySerial() const;
    void setReplySerial(uint32 replySerial);
    std::string destination() const;
    void setDestination(const std::string &destination);
    std::string sender() const;
    void setSender(const std::string &sender);
    std::string signature() const;
    // no setSignature() - setArguments() also sets the signature
    uint32 unixFdCount() const;
    // no setUnixFdCount() - setArguments() also sets the Unix file descriptor count

    bool expectsReply() const; // default true (except for signals, I guess? TODO clarify)
    void setExpectsReply(bool);

    bool autoStartService() const;
    void setAutoStartService(bool);

    bool interactiveAuthorizationAllowed() const; // default true
    void setInteractiveAuthorizationAllowed(bool) const;

    // setArguments also sets the signature header of the message
    void setArguments(Arguments arguments);
    const Arguments &arguments() const;

    std::vector<byte> save();
    void load(const std::vector<byte> &data);
    void deserializeAndTake(chunk memOwnership);

    // The rest of public methods is low-level API that should only be used in very special situations

    void setSerial(uint32 serial);
    uint32 serial() const;

#ifndef DFERRY_SERDES_ONLY
    bool isReceiving() const;
    bool isSending() const;
#endif

private:
    friend class MessagePrivate;
    MessagePrivate *d;
};

#endif // MESSAGE_H
