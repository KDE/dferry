#include "message.h"

#include "basictypeio.h"
#include "iconnection.h"

#include <cassert>
#include <cstring>

#include <iostream>

using namespace std;

// TODO
static const byte thisMachineEndianness = 'l';

// TODO validate when deserializing: check that header-body padding is zero

// TODO think of copying signature from and to output!

Message::Message(int serial)
   : m_io(NoIo),
     m_isByteSwapped(false),
     m_messageType(InvalidMessage),
     m_flags(0), // TODO
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(serial)
{
}

Message::Message()
   : m_io(NoIo),
     m_isByteSwapped(false),
     m_messageType(InvalidMessage),
     m_flags(0), // TODO
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(0)
{
}

Message::Type Message::type() const
{
    return m_messageType;
}

void Message::setType(Type type)
{
    m_buffer.clear(); // dirty
    m_messageType = type;
}


byte Message::flags() const
{
    return m_flags;
}

uint32 Message::protocolVersion() const
{
    return m_protocolVersion;
}

int Message::serial() const
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
        *isPresent = it == m_stringHeaders.end();
    }
    return it == m_stringHeaders.end() ? string() : it->second;
}

bool Message::setStringHeader(VariableHeader header, const string &value)
{
    m_buffer.clear();
    m_stringHeaders[header] = value;
    // TODO error checking / validation
    return true;
}

uint32 Message::intHeader(VariableHeader header, bool *isPresent) const
{
    map<int, uint32>::const_iterator it = m_intHeaders.find(header);
    if (isPresent) {
        *isPresent = it == m_intHeaders.end();
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

void Message::readFrom(IConnection *conn)
{
    if (m_io != NoIo) {
        return;
    }
    conn->addClient(this);
    setIsReadNotificationEnabled(true);
    m_io = ReadIo;
    m_headerLength = 0;
    m_bodyLength = 0;
}

bool Message::isReading() const
{
    return m_io == ReadIo;
}

void Message::writeTo(IConnection *conn)
{
    if (m_io != NoIo) {
        return;
    }
    if (m_buffer.empty() && !fillOutBuffer()) {
        // TODO report error
        return;
    }
    conn->addClient(this);
    setIsWriteNotificationEnabled(true);
    m_io = WriteIo;
}

bool Message::isWriting() const
{
    return m_io == WriteIo;
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

void Message::notifyConnectionReadyRead()
{
    if (m_io != ReadIo) {
        return;
    }
    bool isError = false;
    byte buffer[4096];
    array in;
    do {
        int readMax = 4096;
        if (!m_headerLength) {
            // the message might only consist of the header, so we must be careful to avoid reading
            // data meant for the next message
            readMax = std::max(readMax, int(s_extendedFixedHeaderLength - m_buffer.size()));
        } else {
            // reading variable headers and/or body
            readMax = std::max(readMax, int(m_headerLength + m_bodyLength - m_buffer.size()));
        }

        const bool headersDone = m_headerLength > 0 && m_buffer.size() >= m_headerLength;

        array in = connection()->read(buffer, readMax);
        assert(in.length > 0);

        for (int i = 0; i < in.length; i++) {
            m_buffer.push_back(in.begin[i]);
        }
        if (!headersDone) {
            if (m_headerLength == 0 && m_buffer.size() >= s_extendedFixedHeaderLength
                && !deserializeFixedHeaders()) {
                isError = true;
                break;
            }
            if (m_headerLength > 0 && m_buffer.size() >= m_headerLength && !deserializeVariableHeaders()) {
                isError = true;
                break;
            }
        }
        if (m_headerLength > 0 && m_buffer.size() >= m_headerLength + m_bodyLength) {
            // all done!
            assert(m_buffer.size() == m_headerLength + m_bodyLength);
            setIsReadNotificationEnabled(false);
            m_io = NoIo;
            break;
        }
        if (!connection()->isOpen()) {
            isError = true;
            break;
        }
    } while (in.length);

    if (isError) {
        setIsReadNotificationEnabled(false);
        m_io = NoIo;
        m_buffer.clear();
        // TODO reset other data members
    }
}

void Message::notifyConnectionReadyWrite()
{
    if (m_io != WriteIo) {
        return;
    }
    int written = 0;
    do {
        written = connection()->write(array(&m_buffer.front(), m_buffer.size())); // HACK
        setIsWriteNotificationEnabled(false);
        m_io = NoIo;
        m_buffer.clear();

    } while (written > 0);

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
    byte *base = &m_buffer.front() + s_properFixedHeaderLength;
    array headerData(base, m_headerLength - m_headerPadding - s_properFixedHeaderLength);
    cstring varHeadersSig("a(yv)");
    ArgumentList argList(varHeadersSig, headerData, m_isByteSwapped);

    ArgumentList::ReadCursor reader = argList.beginRead();
    assert(reader.isValid());

    if (reader.state() == ArgumentList::BeginArray) {
        reader.beginArray();
        while (reader.nextArrayEntry()) {
            reader.beginStruct();
            byte headerType = reader.readByte();

            reader.beginVariant();
            switch (headerType) {
            // TODO: proper error handling instead of assertions
            case PathHeader: {
                assert(reader.state() == ArgumentList::ObjectPath);
                cstring str = reader.readObjectPath();
                m_stringHeaders[headerType] = string(reinterpret_cast<const char*>(str.begin), str.length);
                break;
            }
            case InterfaceHeader:
            case MethodHeader:
            case ErrorNameHeader:
            case DestinationHeader:
            case SenderHeader: {
                assert(reader.state() == ArgumentList::String);
                cstring str = reader.readString();
                m_stringHeaders[headerType] = string(reinterpret_cast<const char*>(str.begin), str.length);
                break;
            }
            case ReplySerialHeader:
                // fallthrough, also read uint32
            case UnixFdsHeader: {
                assert(reader.state() == ArgumentList::UnixFd);
                m_intHeaders[headerType] = reader.readUint32();
                break;
            }
            case SignatureHeader: {
                assert(reader.state() == ArgumentList::Signature);
                cstring str = reader.readSignature();
                m_stringHeaders[headerType] = string(reinterpret_cast<const char*>(str.begin), str.length);
                break;
            }
            default:
                break; // ignore unknown headers
            }
            reader.endVariant();
            reader.endStruct();
        }
        reader.endArray();
    } else {
        return false;
    }

    // check that padding is in fact zeroed out
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
        m_stringHeaders[SignatureHeader] = string(reinterpret_cast<const char *>(signature.begin),
                                                  signature.length);
    }

    ArgumentList headerArgs;
    serializeVariableHeaders(&headerArgs);

    // we need to cut out alignment padding bytes 4 to 7 in the variable header data stream because
    // the original dbus code aligns based on address in the final data stream
    // (offset s_properFixedHeaderLength == 12), we align based on address in the ArgumentList's buffer
    // (offset 0) - note that this keeps the stream valid because length is measured from end of padding

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
    ArgumentList::WriteCursor writer = headerArgs->beginWrite();

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
