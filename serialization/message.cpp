#include "message.h"

#include "argumentlist.h"
#include "basictypeio.h"

#include <cassert>
#include <cstring>

// TODO
static const bool isBigEndianMachine = false;

Message::Message(int serial)
   : m_isByteSwapped(false),
     m_messageType(0), // TODO
     m_flags(0), // TODO
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(serial),
     m_headerParser(0)
{
}

enum {
    PATH = 1,
    INTERFACE = 2,
    MEMBER = 3,
    ERROR_NAME = 4,
    REPLY_SERIAL = 5,
    DESTINATION = 6,
    SENDER = 7,
    SIGNATURE = 8,
    UNIX_FDS = 9
};

static const int fixedHeaderLength = 12;

Message::Message(array a)
   : m_data(a),
     m_isByteSwapped(false),
     m_messageType(0), // TODO
     m_flags(0), // TODO
     m_protocolVersion(1),
     m_bodyLength(0),
     m_serial(0),
     m_headerParser(0)
{
    // read the fixed header parts manually
    assert(a.length >= fixedHeaderLength);
    byte *p = a.begin;

    byte endianness = *p++;
    if (endianness == 'l') {
        m_isByteSwapped = isBigEndianMachine;
    } else if (endianness == 'B') {
        m_isByteSwapped = !isBigEndianMachine;
    } else {
        assert(false); // TODO
    }

    // TODO check the values
    m_messageType = *p++;
    m_flags = *p++;
    m_protocolVersion = *p++;

    m_bodyLength = basic::readUint32(p, m_isByteSwapped);
    m_serial = basic::readUint32(p + sizeof(uint32), m_isByteSwapped);

    // prepare and begin variable header parsing
    static const char *sig = "a(yv)";
    array varHeadersSig(const_cast<char *>(sig), strlen(sig));
    array headerData(a.begin + fixedHeaderLength, a.length - fixedHeaderLength);
    m_headerParser = new ArgumentList(varHeadersSig, headerData, m_isByteSwapped);

    parseVariableHeaders();
}

void Message::parseVariableHeaders()
{
    // use ArgumentList to parse the variable header fields
    if (!m_headerParser) {
        return;
    }

    ArgumentList::ReadCursor reader = m_headerParser->beginRead();
    assert(reader.isValid());

    // TODO the isZeroLengthArray stuff is just an API demo(!), we don't actually want to do anything
    //      if the array is empty. remove it when we have tests / examples.
    bool isZeroLengthArray;
    for (reader.beginArray(&isZeroLengthArray); reader.nextArrayEntry(); ) {
        reader.beginStruct();
        byte headerType = reader.readByte();

        reader.beginVariant();
        if (!isZeroLengthArray) {
            switch (headerType) {
            case PATH: {
                assert(reader.state() == ArgumentList::ObjectPath);
                m_stringHeaders[headerType] = reader.readObjectPath();
                break;
            }
            case INTERFACE:
            case MEMBER:
            case ERROR_NAME:
            case DESTINATION:
            case SENDER: {
                assert(reader.state() == ArgumentList::String);
                m_stringHeaders[headerType] = reader.readString();
                break;
            }
            case REPLY_SERIAL:
            case UNIX_FDS: {
                assert(reader.state() == ArgumentList::UnixFd);
                m_intHeaders[headerType] = reader.readUint32();
                break;
            }
            case SIGNATURE: {
                assert(reader.state() == ArgumentList::Signature);
                m_stringHeaders[headerType] = reader.readSignature();
                break;
            }
            default:
                break; // ignore unknown headers
            };
        }
        reader.endVariant();
        reader.endStruct();
    }
    reader.endArray();

    static const int maxMessageLength = 134217728;
    // TODO check header length + message length <= maxMessageLength

    if (false) { // TODO when done parsing...
        delete m_headerParser;
        m_headerParser = 0;
    }
}
