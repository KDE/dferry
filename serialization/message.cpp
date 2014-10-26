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

#include "message.h"
#include "message_p.h"

#include "basictypeio.h"
#include "icompletionclient.h"
#include "iconnection.h"
#include "stringtools.h"

#include <cassert>
#include <cstring>

#include <iostream>
#include <sstream>

using namespace std;

// TODO
static const byte thisMachineEndianness = 'l';

// TODO think of copying signature from and to output!

MessagePrivate::MessagePrivate(Message *parent)
   : m_message(parent),
     m_isByteSwapped(false),
     m_state(Empty),
     m_messageType(Message::InvalidMessage),
     m_flags(0),
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(0),
     m_completionClient(nullptr)
{}

Message::Message()
   : d(new MessagePrivate(this))
{
}

Message::Message(Message &&other)
   : d(other.d)
{
    other.d = nullptr;
    d->m_message = this;
}

Message &Message::operator=(Message &&other)
{
    if (&other != this) {
        delete d;
        d = other.d;
        d->m_message = this;
    }
    return *this;
}

Message::~Message()
{
    delete d;
    d = nullptr;
}

void Message::setCall(const string &path, const string &interface, const string &method)
{
    setType(MethodCallMessage);
    setPath(path);
    setInterface(interface);
    setMethod(method);
}

void Message::setCall(const string &path, const string &method)
{
    setCall(path, string(), method);
}

void Message::setReplyTo(const Message &call)
{
    setType(MethodReturnMessage);
    setDestination(call.sender());
    setReplySerial(call.serial());
}

void Message::setErrorReplyTo(const Message &call, const string &errorName)
{
    setType(ErrorMessage);
    setErrorName(errorName);
    setDestination(call.sender());
    setReplySerial(call.serial());
}

void Message::setSignal(const string &path, const string &interface, const string &method)
{
    setType(SignalMessage);
    setPath(path);
    setInterface(interface);
    setMethod(method);
}

Message *Message::createCall(const string &path, const string &interface, const string &method)
{
    Message *ret = new Message;
    ret->setCall(path, interface, method);
    return ret;
}

Message *Message::createCall(const string &path, const string &method)
{
    Message *ret = new Message;
    ret->setCall(path, method);
    return ret;
}

Message *Message::createReplyTo(const Message &call)
{
    Message *ret = new Message;
    ret->setReplyTo(call);
    return ret;
}

Message *Message::createErrorReplyTo(const Message &call, const string &errorName)
{
    Message *ret = new Message;
    ret->setErrorReplyTo(call, errorName);
    return ret;
}

Message *Message::createSignal(const string &path, const string &interface, const string &method)
{
    Message *ret = new Message;
    ret->setSignal(path, interface, method);
    return ret;
}

struct VarHeaderPrinter
{
    Message::VariableHeader field;
    const char *name;
};

static const int stringHeadersCount = 7;
static VarHeaderPrinter stringHeaderPrinters[stringHeadersCount] = {
    { Message::PathHeader, "path" },
    { Message::InterfaceHeader, "interface" },
    { Message::MethodHeader, "method" },
    { Message::ErrorNameHeader, "error name" },
    { Message::DestinationHeader, "destination" },
    { Message::SenderHeader, "sender" },
    { Message::SignatureHeader, "signature" }
};

static const int intHeadersCount = 2;
static VarHeaderPrinter intHeaderPrinters[intHeadersCount] = {
    { Message::ReplySerialHeader, "reply serial" },
    { Message::UnixFdsHeader, "#unix fds" }
};

static const int messageTypeCount = 5;
static const char *printableMessageTypes[messageTypeCount] = {
    "", // handled in code
    "Method call",
    "Method return",
    "Method error return",
    "Signal"
};

string Message::prettyPrint() const
{
    string ret;
    if (d->m_messageType >= 1 && d->m_messageType < messageTypeCount) {
        ret += printableMessageTypes[d->m_messageType];
    } else {
        return string("Invalid message.\n");
    }

    for (int i = 0; i < stringHeadersCount; i++ ) {
        bool isPresent = false;
        string str = stringHeader(stringHeaderPrinters[i].field, &isPresent);
        if (isPresent) {
            ostringstream os;
            os << "; " << stringHeaderPrinters[i].name << ": \"" << str << '"';
            ret += os.str();
        }
    }

    for (int i = 0; i < intHeadersCount; i++ ) {
        bool isPresent = false;
        uint32 intValue = intHeader(intHeaderPrinters[i].field, &isPresent);
        if (isPresent) {
            ostringstream os;
            os << "; " << intHeaderPrinters[i].name << ": " << intValue;
            ret += os.str();
        }
    }

    ret += '\n';
    ret += d->m_mainArguments.prettyPrint();
    return ret;
}

Message::Type Message::type() const
{
    return d->m_messageType;
}

void Message::setType(Type type)
{
    if (d->m_messageType == type) {
        return;
    }
    d->m_buffer.clear(); // dirty
    d->m_messageType = type;
    setExpectsReply(d->m_messageType == MethodCallMessage);
}

uint32 Message::protocolVersion() const
{
    return d->m_protocolVersion;
}

void Message::setSerial(uint32 serial)
{
    d->m_serial = serial;
}

uint32 Message::serial() const
{
    return d->m_serial;
}

std::string Message::path() const
{
    return stringHeader(PathHeader, 0);
}

void Message::setPath(const std::string &path)
{
    setStringHeader(PathHeader, path);
}

std::string Message::interface() const
{
    return stringHeader(InterfaceHeader, 0);
}

void Message::setInterface(const std::string &interface)
{
    setStringHeader(InterfaceHeader, interface);
}

std::string Message::method() const
{
    return stringHeader(MethodHeader, 0);
}

void Message::setMethod(const std::string &method)
{
    setStringHeader(MethodHeader, method);
}

std::string Message::errorName() const
{
    return stringHeader(ErrorNameHeader, 0);
}

void Message::setErrorName(const std::string &errorName)
{
    setStringHeader(ErrorNameHeader, errorName);
}

uint32 Message::replySerial() const
{
    return intHeader(ReplySerialHeader, 0);
}

void Message::setReplySerial(uint32 replySerial)
{
    setIntHeader(ReplySerialHeader, replySerial);
}

std::string Message::destination() const
{
    return stringHeader(DestinationHeader, 0);
}

void Message::setDestination(const std::string &destination)
{
    setStringHeader(DestinationHeader, destination);
}

std::string Message::sender() const
{
    return stringHeader(SenderHeader, 0);
}

void Message::setSender(const std::string &sender)
{
    setStringHeader(SenderHeader, sender);
}

std::string Message::signature() const
{
    return stringHeader(SignatureHeader, 0);
}

void Message::setSignature(const std::string &signature)
{
    setStringHeader(SignatureHeader, signature);
}

uint32 Message::unixFdCount() const
{
    return intHeader(UnixFdsHeader, 0);
}

void Message::setUnixFdCount(uint32 fdCount)
{
    setIntHeader(UnixFdsHeader, fdCount);
}

string Message::stringHeader(VariableHeader header, bool *isPresent) const
{
    map<int, string>::const_iterator it = d->m_stringHeaders.find(header);
    if (isPresent) {
        *isPresent = it != d->m_stringHeaders.end();
    }
    return it == d->m_stringHeaders.end() ? string() : it->second;
}

bool Message::setStringHeader(VariableHeader header, const string &value)
{
    if (header == SignatureHeader && value.empty()) {
        // The spec allows no signature header when the signature is empty. Make use of that.
        if (d->m_stringHeaders.erase(header)) {
            d->m_buffer.clear();
        }
        return true;
    }

    d->m_buffer.clear();
    d->m_stringHeaders[header] = value;
    // TODO error checking / validation
    return true;
}

uint32 Message::intHeader(VariableHeader header, bool *isPresent) const
{
    map<int, uint32>::const_iterator it = d->m_intHeaders.find(header);
    if (isPresent) {
        *isPresent = it != d->m_intHeaders.end();
    }
    return it == d->m_intHeaders.end() ? 0 : it->second;
}

bool Message::setIntHeader(VariableHeader header, uint32 value)
{
    d->m_buffer.clear();
    d->m_intHeaders[header] = value;
    // TODO error checking / validation
    return true;
}

bool Message::expectsReply() const
{
    return (d->m_flags & MessagePrivate::NoReplyExpectedFlag) == 0;
}

void Message::setExpectsReply(bool expectsReply)
{
    if (expectsReply) {
        d->m_flags &= ~MessagePrivate::NoReplyExpectedFlag;
    } else {
        d->m_flags |= MessagePrivate::NoReplyExpectedFlag;
    }
}

void Message::receive(IConnection *conn)
{
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return;
    }
    conn->addClient(d);
    d->setIsReadNotificationEnabled(true);
    d->m_state = MessagePrivate::Deserializing;
    d->m_headerLength = 0;
    d->m_bodyLength = 0;
}

bool Message::isReceiving() const
{
    return d->m_state == MessagePrivate::Deserializing;
}

void Message::send(IConnection *conn)
{
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return;
    }
    if (d->m_buffer.empty() && !d->fillOutBuffer()) {
        // TODO report error
        return;
    }
    conn->addClient(d);
    d->setIsWriteNotificationEnabled(true);
    d->m_state = MessagePrivate::Serializing;
}

bool Message::isSending() const
{
    return d->m_state == MessagePrivate::Serializing;
}

void Message::setCompletionClient(ICompletionClient *client)
{
    d->m_completionClient = client;
}

void Message::setArgumentList(const ArgumentList &arguments)
{
    d->m_buffer.clear();
    d->m_mainArguments = arguments;
}

const ArgumentList &Message::argumentList() const
{
    return d->m_mainArguments;
}

static const int s_properFixedHeaderLength = 12;
static const int s_extendedFixedHeaderLength = 16;
static const int s_maxMessageLength = 134217728;

// This does not return bool because full validation of the main argument list would take quite
// a few cycles. Validating only the header of the message doesn't seem to be worth it.
void Message::load(const std::vector<byte> &data)
{
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return;
    }
    d->m_headerLength = 0;
    d->m_bodyLength = 0;
    d->m_buffer = data;

    bool ok = d->m_buffer.size() >= s_extendedFixedHeaderLength;
    ok = ok && d->deserializeFixedHeaders();
    ok = ok && d->m_buffer.size() >= d->m_headerLength;
    ok = ok && d->deserializeVariableHeaders();

    if (!ok) {
        d->m_state = MessagePrivate::Empty;
        d->m_buffer.clear();
        return;
    }
    assert(d->m_buffer.size() == d->m_headerLength + d->m_bodyLength);
    std::string sig = signature();
    chunk bodyData(&d->m_buffer.front() + d->m_headerLength, d->m_bodyLength);
    d->m_mainArguments = ArgumentList(cstring(sig.c_str(), sig.length()), bodyData, d->m_isByteSwapped);
    d->m_state = MessagePrivate::Deserialized;
}

void MessagePrivate::notifyConnectionReadyRead()
{
    if (m_state != Deserializing) {
        return;
    }
    bool isError = false;
    byte buffer[4096];
    chunk in;
    do {
        int readMax = 4096;
        if (!m_headerLength) {
            // the message might only consist of the header, so we must be careful to avoid reading
            // data meant for the next message
            readMax = std::min(readMax, int(s_extendedFixedHeaderLength - m_buffer.size()));
        } else {
            // reading variable headers and/or body
            readMax = std::min(readMax, int(m_headerLength + m_bodyLength - m_buffer.size()));
        }

        const bool headersDone = m_headerLength > 0 && m_buffer.size() >= m_headerLength;

        in = connection()->read(buffer, readMax);
        assert(in.length > 0);

        for (int i = 0; i < in.length; i++) {
            m_buffer.push_back(in.begin[i]);
        }
        if (!headersDone) {
            if (m_headerLength == 0 && m_buffer.size() >= s_extendedFixedHeaderLength) {
                if (!deserializeFixedHeaders()) {
                    isError = true;
                    break;
                }
            }
            if (m_headerLength > 0 && m_buffer.size() >= m_headerLength) {
                if (!deserializeVariableHeaders()) {
                    isError = true;
                    break;
                }
            }
        }
        if (m_headerLength > 0 && m_buffer.size() >= m_headerLength + m_bodyLength) {
            // all done!
            assert(m_buffer.size() == m_headerLength + m_bodyLength);
            setIsReadNotificationEnabled(false);
            m_state = Deserialized;
            std::string sig;
            auto it = m_stringHeaders.find(Message::SignatureHeader);
            if (it != m_stringHeaders.end()) {
                sig = it->second;
            }
            chunk bodyData(&m_buffer.front() + m_headerLength, m_bodyLength);
            m_mainArguments = ArgumentList(cstring(sig.c_str(), sig.length()), bodyData, m_isByteSwapped);
            assert(!isError);
            connection()->removeClient(this);
            notifyCompletionClient(); // do not access members after this because it might delete us!
            break;
        }
        if (!connection()->isOpen()) {
            isError = true;
            break;
        }
    } while (in.length);

    if (isError) {
        setIsReadNotificationEnabled(false);
        m_state = Empty;
        m_buffer.clear();
        connection()->removeClient(this);
        notifyCompletionClient();
        // TODO reset other data members
    }
}

std::vector<byte> Message::save()
{
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return vector<byte>();
    }
    if (d->m_buffer.empty() && !d->fillOutBuffer()) {
        // TODO report error?
        return vector<byte>();
    }
    return d->m_buffer;
}

void MessagePrivate::notifyConnectionReadyWrite()
{
    if (m_state != Serializing) {
        return;
    }
    int written = 0;
    do {
        written = connection()->write(chunk(&m_buffer.front(), m_buffer.size())); // HACK
        setIsWriteNotificationEnabled(false);
        m_state = Serialized;
        m_buffer.clear();

    } while (written > 0);
    connection()->removeClient(this);
    assert(connection() == 0);
    notifyCompletionClient();
}

bool MessagePrivate::requiredHeadersPresent() const
{
    if (m_serial == 0 || m_protocolVersion != 1) {
        return false;
    }

    switch (m_messageType) {
    case Message::SignalMessage:
        // required: PathHeader, InterfaceHeader, MethodHeader
        if (!m_stringHeaders.count(Message::InterfaceHeader)) {
            return false;
        }
        // fall through
    case Message::MethodCallMessage:
        // required: PathHeader, MethodHeader
        return m_stringHeaders.count(Message::PathHeader) && m_stringHeaders.count(Message::MethodHeader);

    case Message::ErrorMessage:
        // required: ErrorNameHeader, ReplySerialHeader
        if (!m_stringHeaders.count(Message::ErrorNameHeader)) {
            return false;
        }
        // fall through
    case Message::MethodReturnMessage:
        // required: ReplySerialHeader
        return m_intHeaders.count(Message::ReplySerialHeader);

    case Message::InvalidMessage:
    default:
        break;
    }
    return false;
}

bool MessagePrivate::deserializeFixedHeaders()
{
    assert(m_buffer.size() >= s_extendedFixedHeaderLength);
    byte *p = &m_buffer.front();

    byte endianness = *p++;
    if (endianness != 'l' && endianness != 'B') {
        return false;
    }
    m_isByteSwapped = endianness != thisMachineEndianness;

    // TODO validate the values read here
    m_messageType = static_cast<Message::Type>(*p++);
    m_flags = *p++;
    m_protocolVersion = *p++;

    m_bodyLength = basic::readUint32(p, m_isByteSwapped);
    m_serial = basic::readUint32(p + sizeof(uint32), m_isByteSwapped);
    // peek into the var-length header and use knowledge about array serialization to infer the
    // number of bytes still required for the header
    uint32 varArrayLength = basic::readUint32(p + 2 * sizeof(uint32), m_isByteSwapped);
    int unpaddedHeaderLength = s_extendedFixedHeaderLength + varArrayLength;
    m_headerLength = align(unpaddedHeaderLength, 8);
    m_headerPadding = m_headerLength - unpaddedHeaderLength;

    return m_headerLength + m_bodyLength <= s_maxMessageLength;
}

bool MessagePrivate::deserializeVariableHeaders()
{
    // use ArgumentList to parse the variable header fields
    // HACK: the fake first int argument is there to start the ArgumentList's data 8 byte aligned
    byte *base = &m_buffer.front() + s_properFixedHeaderLength - sizeof(int32);
    chunk headerData(base, m_headerLength - m_headerPadding - s_properFixedHeaderLength + sizeof(int32));
    cstring varHeadersSig("ia(yv)");
    ArgumentList argList(varHeadersSig, headerData, m_isByteSwapped);

    ArgumentList::Reader reader = argList.beginRead();
    assert(reader.isValid());

    if (reader.state() != ArgumentList::Int32) {
        return false;
    }
    reader.readInt32();
    if (reader.state() != ArgumentList::BeginArray) {
        return false;
    }
    reader.beginArray();

    while (reader.nextArrayEntry()) {
        reader.beginStruct();
        byte headerType = reader.readByte();

        reader.beginVariant();
        switch (headerType) {
        // TODO: proper error handling instead of assertions
        case Message::PathHeader: {
            assert(reader.state() == ArgumentList::ObjectPath);
            m_stringHeaders[headerType] = toStdString(reader.readObjectPath());;
            break;
        }
        case Message::InterfaceHeader:
        case Message::MethodHeader:
        case Message::ErrorNameHeader:
        case Message::DestinationHeader:
        case Message::SenderHeader: {
            assert(reader.state() == ArgumentList::String);
            m_stringHeaders[headerType] = toStdString(reader.readString());
            break;
        }
        case Message::ReplySerialHeader:
            assert(reader.state() == ArgumentList::Uint32);
            m_intHeaders[headerType] = reader.readUint32();
            break;
        case Message::UnixFdsHeader:
            assert(reader.state() == ArgumentList::Uint32);
            reader.readUint32(); // discard it, for now
            // TODO
            break;
        case Message::SignatureHeader: {
            assert(reader.state() == ArgumentList::Signature);
            // The spec allows having no signature header, which means "empty signature". However...
            // We do not drop empty signature headers when deserializing, in order to preserve
            // the original message contents. This could be useful for debugging and testing.
            m_stringHeaders[headerType] = toStdString(reader.readSignature());
            break;
        }
        default:
            break; // ignore unknown headers
        }
        reader.endVariant();
        reader.endStruct();
    }
    reader.endArray();

    // check that header->body padding is in fact zero filled
    base = &m_buffer.front();
    for (int i = m_headerLength - m_headerPadding; i < m_headerLength; i++) {
        if (base[i] != '\0') {
            return false;
        }
    }

    return true;
}

bool MessagePrivate::fillOutBuffer()
{
    if (!requiredHeadersPresent()) {
        return false;
    }

    // ### can this be done more cleanly?
    cstring signature = m_mainArguments.signature();
    if (signature.length) {
        m_stringHeaders[Message::SignatureHeader] = toStdString(signature);
    }

    ArgumentList headerArgs;
    serializeVariableHeaders(&headerArgs);

    // we need to cut out alignment padding bytes 4 to 7 in the variable header data stream because
    // the original dbus code aligns based on address in the final data stream
    // (offset s_properFixedHeaderLength == 12), we align based on address in the ArgumentList's buffer
    // (offset 0) - note that our modification keeps the stream valid because length is measured from end
    // of padding

    assert(headerArgs.data().length > 0); // if this fails the headerLength hack will break down

    const int headerLength = s_properFixedHeaderLength + headerArgs.data().length - sizeof(uint32);
    m_headerLength = align(headerLength, 8);
    m_bodyLength = m_mainArguments.data().length;

    if (m_headerLength + m_bodyLength > s_maxMessageLength) {
        // TODO free buffer(s) of headerArgs?
        return false;
    }

    m_buffer.resize(m_headerLength + m_bodyLength);

    serializeFixedHeaders();

    // copy header data: uint32 length...
    memcpy(&m_buffer.front() + s_properFixedHeaderLength, headerArgs.data().begin, sizeof(uint32));
    // skip four bytes of padding and copy the rest
    memcpy(&m_buffer.front() + s_properFixedHeaderLength + sizeof(uint32),
           headerArgs.data().begin + 2 * sizeof(uint32),
           headerArgs.data().length - 2 * sizeof(uint32));
    // zero padding between variable headers and message body
    for (int i = headerLength; i < m_headerLength; i++) {
        m_buffer[i] = '\0';
    }
    // copy message body
    memcpy(&m_buffer.front() + m_headerLength, m_mainArguments.data().begin, m_mainArguments.data().length);
    return true;
}

void MessagePrivate::serializeFixedHeaders()
{
    assert(m_buffer.size() >= s_extendedFixedHeaderLength);
    byte *p = &m_buffer.front();

    *p++ = thisMachineEndianness;
    *p++ = byte(m_messageType);
    *p++ = m_flags;
    *p++ = m_protocolVersion;

    basic::writeUint32(p, m_bodyLength);
    basic::writeUint32(p + sizeof(uint32), m_serial);
}

void MessagePrivate::serializeVariableHeaders(ArgumentList *headerArgs)
{
    ArgumentList::Writer writer = headerArgs->beginWrite();

    // note that we don't have to deal with zero-length arrays because all valid message types require
    // at least one of the variable headers
    writer.beginArray(false);

    for (map<int, uint32>::const_iterator it = m_intHeaders.begin(); it != m_intHeaders.end(); ++it) {
        writer.nextArrayEntry();
        writer.beginStruct();
        writer.writeByte(byte(it->first));
        writer.beginVariant();
        writer.writeUint32(it->second);
        writer.endVariant();
        writer.endStruct();
    }

    // TODO check errors in the writer, e.g. when passing an invalid object path. for efficiency, that
    //      is not checked before because it's done in the writer anyway.

    for (map<int, string>::const_iterator it = m_stringHeaders.begin(); it != m_stringHeaders.end(); ++it) {
        writer.nextArrayEntry();
        writer.beginStruct();
        writer.writeByte(static_cast<byte>(it->first));
        writer.beginVariant();

        switch (it->first) {
        case Message::PathHeader:
            writer.writeObjectPath(cstring(it->second.c_str(), it->second.length()));
            break;
        case Message::InterfaceHeader:
        case Message::MethodHeader:
        case Message::ErrorNameHeader:
        case Message::DestinationHeader:
        case Message::SenderHeader:
            writer.writeString(cstring(it->second.c_str(), it->second.length()));
            break;
        case Message::SignatureHeader:
            writer.writeSignature(cstring(it->second.c_str(), it->second.length()));
            break;
        default:
            assert(false);
        }

        writer.endVariant();
        writer.endStruct();
    }

    writer.endArray();
    writer.finish();
}

void MessagePrivate::notifyCompletionClient()
{
    if (m_completionClient) {
        m_completionClient->notifyCompletion(m_message);
    }
}
