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
#include "malloccache.h"
#include "stringtools.h"

#include <cassert>
#include <cstring>
#include <sstream>
#include <thread>

#include <iostream>

using namespace std;

#ifdef BIGENDIAN
static const byte s_thisMachineEndianness = 'b';
#else
static const byte s_thisMachineEndianness = 'l';
#endif

struct MsgAllocCaches
{
    MallocCache<sizeof(MessagePrivate), 4> msgPrivate;
    MallocCache<256, 4> msgBuffer;
};

thread_local static MsgAllocCaches msgAllocCaches;

static const byte s_storageForHeader[Message::UnixFdsHeader + 1] = {
    0, // dummy entry: there is no enum value for 0
    0xf0 | 0, // PathHeader
    0xf0 | 1, // InterfaceHeader
    0xf0 | 2, // MethodHeader
    0xf0 | 3, // ErrorNameHeader
       0 | 0, // ReplySerialHeader
    0xf0 | 4, // DestinationHeader
    0xf0 | 5, // SenderHeader
    0xf0 | 6, // SignatureHeader
       0 | 1  // UnixFdsHeader
};

static bool isStringHeader(int field)
{
    return s_storageForHeader[field] & 0xf0;
}

static int indexOfHeader(int field)
{
    return s_storageForHeader[field] & ~0xf0;
}

static const Message::VariableHeader s_stringHeaderAtIndex[VarHeaderStorage::s_stringHeaderCount] = {
    Message::PathHeader,
    Message::InterfaceHeader,
    Message::MethodHeader,
    Message::ErrorNameHeader,
    Message::DestinationHeader,
    Message::SenderHeader,
    Message::SignatureHeader
};

static const Message::VariableHeader s_intHeaderAtIndex[VarHeaderStorage::s_intHeaderCount] = {
    Message::ReplySerialHeader,
    Message::UnixFdsHeader
};

VarHeaderStorage::VarHeaderStorage()
{} // initialization values are in class declaration

VarHeaderStorage::VarHeaderStorage(const VarHeaderStorage &other)
{
    // ### very suboptimal
    for (int i = 0; i < Message::UnixFdsHeader + 1; i++) {
        Message::VariableHeader vh = static_cast<Message::VariableHeader>(i);
        if (other.hasHeader(vh)) {
            if (isStringHeader(vh)) {
                setStringHeader(vh, other.stringHeader(vh));
            } else {
                setIntHeader(vh, other.intHeader(vh));
            }
        }
    }
}

VarHeaderStorage::~VarHeaderStorage()
{
    for (int i = 0; i < s_stringHeaderCount; i++) {
        const Message::VariableHeader field = s_stringHeaderAtIndex[i];
        if (hasHeader(field)) {
            stringHeaders()[i].~string();
        }
    }
}

bool VarHeaderStorage::hasHeader(Message::VariableHeader header) const
{
    return m_headerPresenceBitmap & (1u << header);
}

bool VarHeaderStorage::hasStringHeader(Message::VariableHeader header) const
{
    return hasHeader(header) && isStringHeader(header);
}

bool VarHeaderStorage::hasIntHeader(Message::VariableHeader header) const
{
    return hasHeader(header) && !isStringHeader(header);
}

string VarHeaderStorage::stringHeader(Message::VariableHeader header) const
{
    return isStringHeader(header) ? stringHeaders()[indexOfHeader(header)] : string();
}

void VarHeaderStorage::setStringHeader(Message::VariableHeader header, const string &value)
{
    if (!isStringHeader(header)) {
        return;
    }
    const int idx = indexOfHeader(header);
    if (hasHeader(header)) {
        stringHeaders()[idx] = value;
    } else {
        m_headerPresenceBitmap |= 1u << header;
        new(stringHeaders() + idx) string(value);
    }
}

bool VarHeaderStorage::setStringHeader_deser(Message::VariableHeader header, cstring value)
{
    if (hasHeader(header)) {
        return false;
    }
    m_headerPresenceBitmap |= 1u << header;
    new(stringHeaders() + indexOfHeader(header)) string(value.ptr, value.length);
    return true;
}

void VarHeaderStorage::clearStringHeader(Message::VariableHeader header)
{
    if (!isStringHeader(header)) {
        return;
    }
    m_headerPresenceBitmap &= ~(1u << header);
    stringHeaders()[indexOfHeader(header)].~string();
}

uint32 VarHeaderStorage::intHeader(Message::VariableHeader header) const
{
    return hasIntHeader(header) ? m_intHeaders[indexOfHeader(header)] : 0;
}

void VarHeaderStorage::setIntHeader(Message::VariableHeader header, uint32 value)
{
    if (isStringHeader(header)) {
        return;
    }
    m_headerPresenceBitmap |= 1u << header;
    m_intHeaders[indexOfHeader(header)] = value;
}

bool VarHeaderStorage::setIntHeader_deser(Message::VariableHeader header, uint32 value)
{
    if (hasHeader(header)) {
        return false;
    }
    m_headerPresenceBitmap |= 1u << header;
    m_intHeaders[indexOfHeader(header)] = value;
    return true;
}

void VarHeaderStorage::clearIntHeader(Message::VariableHeader header)
{
    if (isStringHeader(header)) {
        return;
    }
    m_headerPresenceBitmap &= ~(1u << header);
}

// TODO think of copying signature from and to output!

MessagePrivate::MessagePrivate(Message *parent)
   : m_message(parent),
     m_bufferPos(0),
     m_isByteSwapped(false),
     m_state(Empty),
     m_messageType(Message::InvalidMessage),
     m_flags(0),
     m_protocolVersion(1),
     m_dirty(true),
     m_headerLength(0),
     m_headerPadding(0),
     m_bodyLength(0),
     m_serial(0),
     m_completionClient(nullptr)
{}

MessagePrivate::MessagePrivate(const MessagePrivate &other, Message *parent)
   : m_message(parent),
     m_bufferPos(other.m_bufferPos),
     m_isByteSwapped(other.m_isByteSwapped),
     m_state(other.m_state),
     m_messageType(other.m_messageType),
     m_flags(other.m_flags),
     m_protocolVersion(other.m_protocolVersion),
     m_dirty(other.m_dirty),
     m_headerLength(other.m_headerLength),
     m_headerPadding(other.m_headerPadding),
     m_bodyLength(other.m_bodyLength),
     m_serial(other.m_serial),
     m_error(other.m_error),
     m_mainArguments(other.m_mainArguments),
     m_varHeaders(other.m_varHeaders),
     m_completionClient(nullptr)
{
    if (other.m_buffer.ptr) {
        // we don't keep pointers into the buffer (only indexes), right? right?
        m_buffer.ptr = static_cast<byte *>(malloc(other.m_buffer.length));
        m_buffer.length = other.m_buffer.length;
        // Simplification: don't try to figure out which part of other.m_buffer contains "valid" data,
        // just copy everything.
        memcpy(m_buffer.ptr, other.m_buffer.ptr, other.m_buffer.length);
    } else {
        assert(!m_buffer.length);
    }
    // ### Maybe warn when copying a Message which is currently (de)serializing. It might even be impossible
    //     to do that from client code. If that is the case, the "warning" could even be an assertion because
    //     we should never do such a thing.
}

Message::Message()
   : d(new(msgAllocCaches.msgPrivate.allocate()) MessagePrivate(this))
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
        d->~MessagePrivate();
        msgAllocCaches.msgPrivate.free(d);
        d = other.d;
        other.d = nullptr;
        d->m_message = this;
    }
    return *this;
}

Message::Message(const Message &other)
   : d(nullptr)
{
    if (!other.d) {
        return;
    }
    d = new(msgAllocCaches.msgPrivate.allocate()) MessagePrivate(*other.d, this);
}

Message &Message::operator=(const Message &other)
{
    if (this == &other) {
        return *this;
    }
    if (d) {
        d->~MessagePrivate();
        msgAllocCaches.msgPrivate.free(d);
        if (other.d) {
            // ### can be optimized by implementing and using assignment of MessagePrivate
            d = new(msgAllocCaches.msgPrivate.allocate()) MessagePrivate(*other.d, this);
        } else {
            d = nullptr;
        }
    } else {
        if (other.d) {
            d = new(msgAllocCaches.msgPrivate.allocate()) MessagePrivate(*other.d, this);
        }
    }
    return *this;
}


Message::~Message()
{
    if (d) {
        d->clearBuffer();
        d->~MessagePrivate();
        msgAllocCaches.msgPrivate.free(d);
        d = nullptr;
    }
}

Error Message::error() const
{
    return d->m_error;
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

Message Message::createCall(const string &path, const string &interface, const string &method)
{
    Message ret;
    ret.setCall(path, interface, method);
    return ret;
}

Message Message::createCall(const string &path, const string &method)
{
    Message ret;
    ret.setCall(path, method);
    return ret;
}

Message Message::createReplyTo(const Message &call)
{
    Message ret;
    ret.setReplyTo(call);
    return ret;
}

Message Message::createErrorReplyTo(const Message &call, const string &errorName)
{
    Message ret;
    ret.setErrorReplyTo(call, errorName);
    return ret;
}

Message Message::createSignal(const string &path, const string &interface, const string &method)
{
    Message ret;
    ret.setSignal(path, interface, method);
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

    ostringstream os;
    for (int i = 0; i < stringHeadersCount; i++ ) {
        bool isPresent = false;
        string str = stringHeader(stringHeaderPrinters[i].field, &isPresent);
        if (isPresent) {
            os << "; " << stringHeaderPrinters[i].name << ": \"" << str << '"';

        }
    }
    for (int i = 0; i < intHeadersCount; i++ ) {
        bool isPresent = false;
        uint32 intValue = intHeader(intHeaderPrinters[i].field, &isPresent);
        if (isPresent) {
            os << "; " << intHeaderPrinters[i].name << ": " << intValue;
        }
    }
    ret += os.str();

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
    d->m_dirty = true;
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
    const bool exists = d->m_varHeaders.hasStringHeader(header);
    if (isPresent) {
        *isPresent = exists;
    }
    return exists ? d->m_varHeaders.stringHeader(header) : string();
}

void Message::setStringHeader(VariableHeader header, const string &value)
{
    if (header == SignatureHeader) {
        // ### warning? - this is a public method, and setting the signature separately does not make sense
        return;
    }
    d->m_dirty = true;
    d->m_varHeaders.setStringHeader(header, value);
}

uint32 Message::intHeader(VariableHeader header, bool *isPresent) const
{
    const bool exists = d->m_varHeaders.hasIntHeader(header);
    if (isPresent) {
        *isPresent = exists;
    }
    return d->m_varHeaders.intHeader(header);
}

void Message::setIntHeader(VariableHeader header, uint32 value)
{
    d->m_dirty = true;
    d->m_varHeaders.setIntHeader(header, value);
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

void MessagePrivate::receive(IConnection *conn)
{
    if (m_state > LastSteadyState) {
        std::cerr << "MessagePrivate::receive() Error A.\n";
        return;
    }
    conn->addClient(this);
    setReadNotificationEnabled(true);
    m_state = MessagePrivate::Deserializing;
    m_headerLength = 0;
    m_bodyLength = 0;
}

bool Message::isReceiving() const
{
    return d->m_state == MessagePrivate::Deserializing;
}

void MessagePrivate::send(IConnection *conn)
{
    if (!m_buffer.length && !serialize()) {
        std::cerr << "MessagePrivate::send() Error A.\n";
        // m_error.setCode();
        // notifyCompletionClient(); would call into Transceiver, but it's easer for Transceiver to handle
        //                           the error from non-callback code, directly in the caller of send().
        return;
    }
    if (m_state > MessagePrivate::LastSteadyState) {
        std::cerr << "MessagePrivate::send() Error B.\n";
        // TODO error feedback
        return;
    }
    conn->addClient(this);
    setWriteNotificationEnabled(true);
    m_state = MessagePrivate::Serializing;
}

bool Message::isSending() const
{
    return d->m_state == MessagePrivate::Serializing;
}

void MessagePrivate::setCompletionClient(ICompletionClient *client)
{
    m_completionClient = client;
}

void Message::setArguments(Arguments arguments)
{
    d->m_dirty = true;
    d->m_error = arguments.error();
    d->m_mainArguments = std::move(arguments);
}

const Arguments &Message::arguments() const
{
    return d->m_mainArguments;
}

static const uint32 s_properFixedHeaderLength = 12;
static const uint32 s_extendedFixedHeaderLength = 16;
static const uint32 s_maxMessageLength = 134217728;

// This does not return bool because full validation of the main arguments would take quite
// a few cycles. Validating only the header of the message doesn't seem to be worth it.
void Message::load(const std::vector<byte> &data)
{
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return;
    }
    d->m_headerLength = 0;
    d->m_bodyLength = 0;

    d->clearBuffer();
    d->m_buffer.length = data.size();
    d->m_bufferPos = d->m_buffer.length;
    d->m_buffer.ptr = reinterpret_cast<byte *>(malloc(d->m_buffer.length));
    memcpy(d->m_buffer.ptr, &data[0], d->m_buffer.length);

    bool ok = d->m_buffer.length >= s_extendedFixedHeaderLength;
    ok = ok && d->deserializeFixedHeaders();
    ok = ok && d->m_buffer.length >= d->m_headerLength;
    ok = ok && d->deserializeVariableHeaders();
    ok = ok && d->m_buffer.length == d->m_headerLength + d->m_bodyLength;

    if (!ok) {
        d->m_state = MessagePrivate::Empty;
        d->clearBuffer();
        return;
    }

    std::string sig = signature();
    chunk bodyData(d->m_buffer.ptr + d->m_headerLength, d->m_bodyLength);
    d->m_mainArguments = Arguments(nullptr, cstring(sig.c_str(), sig.length()),
                                      bodyData, d->m_isByteSwapped);
    d->m_state = MessagePrivate::Deserialized;
}

void MessagePrivate::notifyConnectionReadyRead()
{
    if (m_state != Deserializing) {
        return;
    }
    bool isError = false;
    chunk in;
    do {
        uint32 readMax = 0;
        if (!m_headerLength) {
            // the message might only consist of the header, so we must be careful to avoid reading
            // data meant for the next message
            readMax = s_extendedFixedHeaderLength - m_bufferPos;
        } else {
            // reading variable headers and/or body
            readMax = m_headerLength + m_bodyLength - m_bufferPos;
        }
        reserveBuffer(m_bufferPos + readMax);

        const bool headersDone = m_headerLength > 0 && m_bufferPos >= m_headerLength;

        in = connection()->read(m_buffer.ptr + m_bufferPos, readMax);
        m_bufferPos += in.length;
        assert(m_bufferPos <= m_buffer.length);

        if (!headersDone) {
            if (m_headerLength == 0 && m_bufferPos >= s_extendedFixedHeaderLength) {
                if (!deserializeFixedHeaders()) {
                    isError = true;
                    break;
                }
            }
            if (m_headerLength > 0 && m_bufferPos >= m_headerLength) {
                if (!deserializeVariableHeaders()) {
                    isError = true;
                    break;
                }
            }
        }
        if (m_headerLength > 0 && m_bufferPos >= m_headerLength + m_bodyLength) {
            // all done!
            assert(m_bufferPos == m_headerLength + m_bodyLength);
            setReadNotificationEnabled(false);
            m_state = Deserialized;
            std::string sig;
            if (m_varHeaders.hasStringHeader(Message::SignatureHeader)) {
                sig = m_varHeaders.stringHeader(Message::SignatureHeader);
            }
            chunk bodyData(m_buffer.ptr + m_headerLength, m_bodyLength);
            m_mainArguments = Arguments(nullptr, cstring(sig.c_str(), sig.length()),
                                           bodyData, m_isByteSwapped);
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
        setReadNotificationEnabled(false);
        m_state = Empty;
        clearBuffer();
        connection()->removeClient(this);
        notifyCompletionClient();
        // TODO reset other data members, generally revisit error handling to make it robust
    }
}

std::vector<byte> Message::save()
{
    vector<byte> ret;
    if (d->m_state > MessagePrivate::LastSteadyState) {
        return ret;
    }
    if (!d->m_buffer.length && !d->serialize()) {
        // TODO report error?
        return ret;
    }
    ret.reserve(d->m_buffer.length);
    for (uint32 i = 0; i < d->m_buffer.length; i++) {
        ret.push_back(d->m_buffer.ptr[i]);
    }
    return ret;
}

void MessagePrivate::notifyConnectionReadyWrite()
{
    if (m_state != Serializing) {
        return;
    }
    while (true) {
        assert(m_buffer.length >= m_bufferPos);
        const uint32 toWrite = m_buffer.length - m_bufferPos;
        if (!toWrite) {
            setWriteNotificationEnabled(false);
            m_state = Serialized;
            clearBuffer();
            connection()->removeClient(this);
            assert(connection() == nullptr);
            notifyCompletionClient();
            break;
        }
        uint32 written = connection()->write(chunk(m_buffer.ptr + m_bufferPos, toWrite));
        if (written <= 0) {
            // TODO error handling
            break;
        }
        m_bufferPos += written;
    }
}

bool MessagePrivate::requiredHeadersPresent()
{
    m_error = checkRequiredHeaders();
    return m_error.isError();
}

Error MessagePrivate::checkRequiredHeaders() const
{
    if (m_serial == 0) {
        return Error::MessageSerial;
    }
    if (m_protocolVersion != 1) {
        return Error::MessageProtocolVersion;
    }

    // might want to check for DestinationHeader if the connection is a bus (not peer-to-peer)
    // very strange that this isn't in the spec!

    switch (m_messageType) {
    case Message::SignalMessage:
        // required: PathHeader, InterfaceHeader, MethodHeader
        if (!m_varHeaders.hasStringHeader(Message::InterfaceHeader)) {
            return Error::MessageInterface;
        }
        // fall through
    case Message::MethodCallMessage:
        // required: PathHeader, MethodHeader
        if (!m_varHeaders.hasStringHeader(Message::PathHeader)) {
            return Error::MessagePath;
        }
        if (!m_varHeaders.hasStringHeader(Message::MethodHeader)) {
            return Error::MessageMethod;
        }

    case Message::ErrorMessage:
        // required: ErrorNameHeader, ReplySerialHeader
        if (!m_varHeaders.hasStringHeader(Message::ErrorNameHeader)) {
            return Error::MessageErrorName;
        }
        // fall through
    case Message::MethodReturnMessage:
        // required: ReplySerialHeader
        if (!m_varHeaders.hasIntHeader(Message::ReplySerialHeader) ) {
            return Error::MessageReplySerial;
        }

    case Message::InvalidMessage:
    default:
        return Error::MessageType;
    }

    return Error::NoError;
}

bool MessagePrivate::deserializeFixedHeaders()
{
    assert(m_bufferPos >= s_extendedFixedHeaderLength);
    byte *p = m_buffer.ptr;

    byte endianness = *p++;
    if (endianness != 'l' && endianness != 'B') {
        return false;
    }
    m_isByteSwapped = endianness != s_thisMachineEndianness;

    // TODO validate the values read here
    m_messageType = static_cast<Message::Type>(*p++);
    m_flags = *p++;
    m_protocolVersion = *p++;

    m_bodyLength = basic::readUint32(p, m_isByteSwapped);
    m_serial = basic::readUint32(p + sizeof(uint32), m_isByteSwapped);
    // peek into the var-length header and use knowledge about array serialization to infer the
    // number of bytes still required for the header
    uint32 varArrayLength = basic::readUint32(p + 2 * sizeof(uint32), m_isByteSwapped);
    uint32 unpaddedHeaderLength = s_extendedFixedHeaderLength + varArrayLength;
    m_headerLength = align(unpaddedHeaderLength, 8);
    m_headerPadding = m_headerLength - unpaddedHeaderLength;

    return m_headerLength + m_bodyLength <= s_maxMessageLength;
}

bool MessagePrivate::deserializeVariableHeaders()
{
    // use Arguments to parse the variable header fields
    // HACK: the fake first int argument is there to start the Arguments's data 8 byte aligned
    byte *base = m_buffer.ptr + s_properFixedHeaderLength - sizeof(int32);
    chunk headerData(base, m_headerLength - m_headerPadding - s_properFixedHeaderLength + sizeof(int32));
    cstring varHeadersSig("ia(yv)");
    Arguments argList(nullptr, varHeadersSig, headerData, m_isByteSwapped);

    Arguments::Reader reader(argList);
    assert(reader.isValid());

    if (reader.state() != Arguments::Int32) {
        return false;
    }
    reader.readInt32();
    if (reader.state() != Arguments::BeginArray) {
        return false;
    }
    reader.beginArray();

    while (reader.nextArrayEntry()) {
        reader.beginStruct();
        const byte headerField = reader.readByte();
        if (headerField < Message::PathHeader || headerField > Message::UnixFdsHeader) {
            return false;
        }
        const Message::VariableHeader eHeader = static_cast<Message::VariableHeader>(headerField);

        reader.beginVariant();

        bool ok = true; // short-circuit evaluation ftw
        if (isStringHeader(headerField)) {
            if (headerField == Message::PathHeader) {
                ok = ok && reader.state() == Arguments::ObjectPath;
                ok = ok && m_varHeaders.setStringHeader_deser(eHeader, reader.readObjectPath());
            } else if (headerField == Message::SignatureHeader) {
                ok = ok && reader.state() == Arguments::Signature;
                // The spec allows having no signature header, which means "empty signature". However...
                // We do not drop empty signature headers when deserializing, in order to preserve
                // the original message contents. This could be useful for debugging and testing.
                ok = ok && m_varHeaders.setStringHeader_deser(eHeader, reader.readSignature());
            } else {
                ok = ok && reader.state() == Arguments::String;
                ok = ok && m_varHeaders.setStringHeader_deser(eHeader, reader.readString());
            }
        } else {
            ok = ok && reader.state() == Arguments::Uint32;
            if (headerField == Message::UnixFdsHeader) {
                reader.readUint32(); // discard it, for now (TODO)
            } else {
                ok = ok && m_varHeaders.setIntHeader_deser(eHeader, reader.readUint32());
            }
        }

        if (!ok) {
            return false;
        }
        reader.endVariant();
        reader.endStruct();
    }
    reader.endArray();

    // check that header->body padding is in fact zero filled
    base = m_buffer.ptr;
    for (uint32 i = m_headerLength - m_headerPadding; i < m_headerLength; i++) {
        if (base[i] != '\0') {
            return false;
        }
    }

    return true;
}

bool MessagePrivate::serialize()
{
    if (!m_dirty) {
        return true;
    }
    clearBuffer();

    if (m_error.isError() || !requiredHeadersPresent()) {
        return false;
    }

    // ### can this be done more cleanly?
    cstring signature = m_mainArguments.signature();
    if (signature.length) {
        m_varHeaders.setStringHeader(Message::SignatureHeader, toStdString(signature));
    }

    Arguments headerArgs = serializeVariableHeaders();

    // we need to cut out alignment padding bytes 4 to 7 in the variable header data stream because
    // the original dbus code aligns based on address in the final data stream
    // (offset s_properFixedHeaderLength == 12), we align based on address in the Arguments's buffer
    // (offset 0) - note that our modification keeps the stream valid because length is measured from end
    // of padding

    assert(headerArgs.data().length > 0); // if this fails the headerLength hack will break down

    const uint32 unalignedHeaderLength = s_properFixedHeaderLength + headerArgs.data().length - sizeof(uint32);
    m_headerLength = align(unalignedHeaderLength, 8);
    m_bodyLength = m_mainArguments.data().length;
    const uint32 messageLength = m_headerLength + m_bodyLength;

    if (messageLength > s_maxMessageLength) {
        return false;
    }

    reserveBuffer(messageLength);

    serializeFixedHeaders();

    // copy header data: uint32 length...
    memcpy(m_buffer.ptr + s_properFixedHeaderLength, headerArgs.data().ptr, sizeof(uint32));
    // skip four bytes of padding and copy the rest
    memcpy(m_buffer.ptr + s_properFixedHeaderLength + sizeof(uint32),
           headerArgs.data().ptr + 2 * sizeof(uint32),
           headerArgs.data().length - 2 * sizeof(uint32));
    // zero padding between variable headers and message body
    for (uint32 i = unalignedHeaderLength; i < m_headerLength; i++) {
        m_buffer.ptr[i] = '\0';
    }
    // copy message body
    memcpy(m_buffer.ptr + m_headerLength, m_mainArguments.data().ptr, m_mainArguments.data().length);
    m_bufferPos = m_headerLength + m_mainArguments.data().length;
    assert(m_bufferPos <= m_buffer.length);

    // for the upcoming message sending, "reuse" m_bufferPos for read position (formerly write position),
    // and m_buffer.length for end of data to read (formerly buffer capacity)
    m_buffer.length = m_bufferPos;
    m_bufferPos = 0;

    m_dirty = false;
    return true;
}

void MessagePrivate::serializeFixedHeaders()
{
    assert(m_buffer.length >= s_extendedFixedHeaderLength);
    byte *p = m_buffer.ptr;

    *p++ = s_thisMachineEndianness;
    *p++ = byte(m_messageType);
    *p++ = m_flags;
    *p++ = m_protocolVersion;

    basic::writeUint32(p, m_bodyLength);
    basic::writeUint32(p + sizeof(uint32), m_serial);
}

static void doVarHeaderPrologue(Arguments::Writer *writer, Message::VariableHeader field)
{
    writer->nextArrayEntry();
    writer->beginStruct();
    writer->writeByte(byte(field));
    writer->beginVariant();
}

static void doVarHeaderEpilogue(Arguments::Writer *writer)
{
    writer->endVariant();
    writer->endStruct();
}

Arguments MessagePrivate::serializeVariableHeaders()
{
    Arguments::Writer writer;

    // note that we don't have to deal with empty arrays because all valid message types require
    // at least one of the variable headers
    writer.beginArray();

    for (int i = 0; i < VarHeaderStorage::s_stringHeaderCount; i++) {
        const Message::VariableHeader field = s_stringHeaderAtIndex[i];
        if (m_varHeaders.hasHeader(field)) {
            doVarHeaderPrologue(&writer, field);

            const string &str = m_varHeaders.stringHeaders()[i];
            if (field == Message::PathHeader) {
                writer.writeObjectPath(cstring(str.c_str(), str.length()));
            } else if (field == Message::SignatureHeader) {
                writer.writeSignature(cstring(str.c_str(), str.length()));
            } else {
                writer.writeString(cstring(str.c_str(), str.length()));
            }

            doVarHeaderEpilogue(&writer);

            if (unlikely(writer.error().isError())) {
                static const Error::Code stringHeaderErrors[VarHeaderStorage::s_stringHeaderCount] = {
                    Error::MessagePath,
                    Error::MessageInterface,
                    Error::MessageMethod,
                    Error::MessageErrorName,
                    Error::MessageDestination,
                    Error::MessageSender,
                    Error::MessageSignature
                };
                m_error.setCode(stringHeaderErrors[i]);
                return Arguments();
            }
        }
    }

    for (int i = 0; i < VarHeaderStorage::s_intHeaderCount; i++) {
        const Message::VariableHeader field = s_intHeaderAtIndex[i];
        if (m_varHeaders.hasHeader(field)) {
            doVarHeaderPrologue(&writer, field);
            writer.writeUint32(m_varHeaders.m_intHeaders[i]);
            doVarHeaderEpilogue(&writer);
        }
    }

    writer.endArray();
    return writer.finish();
}

void MessagePrivate::clearBuffer()
{
    if (m_buffer.ptr) {
        free (m_buffer.ptr);
        m_buffer = chunk();
        m_bufferPos = 0;
    } else {
        assert(m_buffer.length == 0);
        assert(m_bufferPos == 0);
    }
}

static uint32 nextPowerOf2(uint32 x)
{
    --x;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    return ++x;
}

void MessagePrivate::reserveBuffer(uint32 newLen)
{
    const uint32 oldLen = m_buffer.length;
    if (newLen <= oldLen) {
        return;
    }
    if (newLen <= 256) {
        assert(oldLen == 0);
        newLen = 256;
        m_buffer.ptr = reinterpret_cast<byte *>(msgAllocCaches.msgBuffer.allocate());
    } else {
        newLen = nextPowerOf2(newLen);
        if (oldLen == 256) {
            byte *newAlloc = reinterpret_cast<byte *>(malloc(newLen));
            memcpy(newAlloc, m_buffer.ptr, oldLen);

            msgAllocCaches.msgBuffer.free(m_buffer.ptr);
            m_buffer.ptr = newAlloc;
        } else {
            m_buffer.ptr = reinterpret_cast<byte *>(realloc(m_buffer.ptr, newLen));
        }
    }

    m_buffer.length = newLen;
}

void MessagePrivate::notifyCompletionClient()
{
    if (m_completionClient) {
        m_completionClient->notifyCompletion(m_message);
    }
}
