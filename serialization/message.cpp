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

Message::Message()
   : m_isByteSwapped(false),
     m_state(Empty),
     m_messageType(InvalidMessage),
     m_flags(0),
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(0),
     m_completionClient(0)
{
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
    if (m_messageType >= 1 && m_messageType < messageTypeCount) {
        ret += printableMessageTypes[m_messageType];
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
    ret += m_mainArguments.prettyPrint();
    return ret;
}

Message::Type Message::type() const
{
    return m_messageType;
}

void Message::setType(Type type)
{
    if (m_messageType == type) {
        return;
    }
    m_buffer.clear(); // dirty
    m_messageType = type;
    setExpectsReply(m_messageType == MethodCallMessage);
}

uint32 Message::protocolVersion() const
{
    return m_protocolVersion;
}

void Message::setSerial(uint32 serial)
{
    m_serial = serial;
}

uint32 Message::serial() const
{
    return m_serial;
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
    map<int, string>::const_iterator it = m_stringHeaders.find(header);
    if (isPresent) {
        *isPresent = it != m_stringHeaders.end();
    }
    return it == m_stringHeaders.end() ? string() : it->second;
}

bool Message::setStringHeader(VariableHeader header, const string &value)
{
    if (header == SignatureHeader && value.empty()) {
        // The spec allows no signature header when the signature is empty. Make use of that.
        if (m_stringHeaders.erase(header)) {
            m_buffer.clear();
        }
        return true;
    }

    m_buffer.clear();
    m_stringHeaders[header] = value;
    // TODO error checking / validation
    return true;
}

uint32 Message::intHeader(VariableHeader header, bool *isPresent) const
{
    map<int, uint32>::const_iterator it = m_intHeaders.find(header);
    if (isPresent) {
        *isPresent = it != m_intHeaders.end();
    }
    return it == m_intHeaders.end() ? 0 : it->second;
}

bool Message::setIntHeader(VariableHeader header, uint32 value)
{
    m_buffer.clear();
    m_intHeaders[header] = value;
    // TODO error checking / validation
    return true;
}

bool Message::expectsReply() const
{
    return (m_flags & NoReplyExpectedFlag) == 0;
}

void Message::setExpectsReply(bool expectsReply)
{
    if (expectsReply) {
        m_flags &= ~NoReplyExpectedFlag;
    } else {
        m_flags |= NoReplyExpectedFlag;
    }
}

void Message::receive(IConnection *conn)
{
    if (m_state > LastSteadyState) {
        return;
    }
    conn->addClient(this);
    setIsReadNotificationEnabled(true);
    m_state = Deserializing;
    m_headerLength = 0;
    m_bodyLength = 0;
}

bool Message::isReceiving() const
{
    return m_state == Deserializing;
}

void Message::send(IConnection *conn)
{
    if (m_state > LastSteadyState) {
        return;
    }
    if (m_buffer.empty() && !fillOutBuffer()) {
        // TODO report error
        return;
    }
    conn->addClient(this);
    setIsWriteNotificationEnabled(true);
    m_state = Serializing;
}

bool Message::isSending() const
{
    return m_state == Serializing;
}

void Message::setCompletionClient(ICompletionClient *client)
{
    m_completionClient = client;
}

void Message::setArgumentList(const ArgumentList &arguments)
{
    m_buffer.clear();
    m_mainArguments = arguments;
}

const ArgumentList &Message::argumentList() const
{
    return m_mainArguments;
}

static const int s_properFixedHeaderLength = 12;
static const int s_extendedFixedHeaderLength = 16;
static const int s_maxMessageLength = 134217728;

// This does not return bool because full validation of the main argument list would take quite
// a few cycles. Validating only the header of the message doesn't seem to be worth it.
void Message::load(const std::vector<byte> &data)
{
    if (m_state > LastSteadyState) {
        return;
    }
    m_headerLength = 0;
    m_bodyLength = 0;
    m_buffer = data;

    bool ok = m_buffer.size() >= s_extendedFixedHeaderLength;
    ok = ok && deserializeFixedHeaders();
    ok = ok && m_buffer.size() >= m_headerLength;
    ok = ok && deserializeVariableHeaders();

    if (!ok) {
        m_state = Empty;
        m_buffer.clear();
        return;
    }
    assert(m_buffer.size() == m_headerLength + m_bodyLength);
    std::string sig = signature();
    chunk bodyData(&m_buffer.front() + m_headerLength, m_bodyLength);
    m_mainArguments = ArgumentList(cstring(sig.c_str(), sig.length()), bodyData, m_isByteSwapped);
    m_state = Deserialized;
}

void Message::notifyConnectionReadyRead()
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
            std::string sig = signature();
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
    if (m_state > LastSteadyState) {
        return vector<byte>();
    }
    if (m_buffer.empty() && !fillOutBuffer()) {
        // TODO report error?
        return vector<byte>();
    }
    return m_buffer;
}

void Message::notifyConnectionReadyWrite()
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

bool Message::requiredHeadersPresent() const
{
    if (m_serial == 0 || m_protocolVersion != 1) {
        return false;
    }

    switch (m_messageType) {
    case SignalMessage:
        // required: PathHeader, InterfaceHeader, MethodHeader
        if (!m_stringHeaders.count(InterfaceHeader)) {
            return false;
        }
        // fall through
    case MethodCallMessage:
        // required: PathHeader, MethodHeader
        return m_stringHeaders.count(PathHeader) && m_stringHeaders.count(MethodHeader);

    case ErrorMessage:
        // required: ErrorNameHeader, ReplySerialHeader
        if (!m_stringHeaders.count(ErrorNameHeader)) {
            return false;
        }
        // fall through
    case MethodReturnMessage:
        // required: ReplySerialHeader
        return m_intHeaders.count(ReplySerialHeader);

    case InvalidMessage:
    default:
        break;
    }
    return false;
}

bool Message::deserializeFixedHeaders()
{
    assert(m_buffer.size() >= s_extendedFixedHeaderLength);
    byte *p = &m_buffer.front();

    byte endianness = *p++;
    if (endianness != 'l' && endianness != 'B') {
        return false;
    }
    m_isByteSwapped = endianness != thisMachineEndianness;

    // TODO validate the values read here
    m_messageType = static_cast<Type>(*p++);
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

bool Message::deserializeVariableHeaders()
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
        case PathHeader: {
            assert(reader.state() == ArgumentList::ObjectPath);
            m_stringHeaders[headerType] = toStdString(reader.readObjectPath());;
            break;
        }
        case InterfaceHeader:
        case MethodHeader:
        case ErrorNameHeader:
        case DestinationHeader:
        case SenderHeader: {
            assert(reader.state() == ArgumentList::String);
            m_stringHeaders[headerType] = toStdString(reader.readString());
            break;
        }
        case ReplySerialHeader:
            assert(reader.state() == ArgumentList::Uint32);
            m_intHeaders[headerType] = reader.readUint32();
            break;
        case UnixFdsHeader:
            assert(reader.state() == ArgumentList::Uint32);
            reader.readUint32(); // discard it, for now
            // TODO
            break;
        case SignatureHeader: {
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

bool Message::fillOutBuffer()
{
    if (!requiredHeadersPresent()) {
        return false;
    }

    // ### can this be done more cleanly?
    cstring signature = m_mainArguments.signature();
    if (signature.length) {
        m_stringHeaders[SignatureHeader] = toStdString(signature);
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

void Message::serializeFixedHeaders()
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

void Message::serializeVariableHeaders(ArgumentList *headerArgs)
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
        case PathHeader:
            writer.writeObjectPath(cstring(it->second.c_str(), it->second.length()));
            break;
        case InterfaceHeader:
        case MethodHeader:
        case ErrorNameHeader:
        case DestinationHeader:
        case SenderHeader:
            writer.writeString(cstring(it->second.c_str(), it->second.length()));
            break;
        case SignatureHeader:
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

void Message::notifyCompletionClient()
{
    if (m_completionClient) {
        m_completionClient->notifyCompletion(this);
    }
}
