#include "authnegotiator.h"

#include "iconnection.h"
#include "stringtools.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

AuthNegotiator::AuthNegotiator(IConnection *connection)
   : m_connection(connection),
     m_line(m_lineBuffer, 0)
{
    connection->setClient(this);
    byte nullBuf[1] = { 0 };
    connection->write(array(nullBuf, 1));

    // no idea why the uid is first encoded to ascii and the ascii to hex...
    uid_t uid = geteuid();
    stringstream uidDecimal;
    uidDecimal << uid;
    string extLine = "AUTH EXTERNAL " + hexEncode(uidDecimal.str()) + "\r\n";
    cout << extLine;
    connection->write(array(extLine.c_str(), extLine.length()));

    cstring negotiateLine("NEGOTIATE_UNIX_FD\r\n");
    connection->write(array(negotiateLine.begin, negotiateLine.length));
    cstring beginLine("BEGIN\r\n");
    connection->write(array(beginLine.begin, beginLine.length));
}

void AuthNegotiator::notifyConnectionReadyRead()
{
    while (readLine()) {
        advanceState();
    }
}

bool AuthNegotiator::readLine()
{
    // don't care about performance here, this doesn't run often or process much data
    if (isEndOfLine()) {
        m_line.length = 0; // start a new line
    }
    while (m_connection->availableBytesForReading()) {
        array in = m_connection->read(1);
        // TODO properly handle the conditions that we currently assert
        assert(in.length == 1);
        assert(m_line.length + 1 < m_maxLineLength); // room for current letter and '\0' // TODO unittest
        m_line.begin[m_line.length++] = in.begin[0];

        if (isEndOfLine()) {
            m_line.begin[m_line.length] = '\0'; // maintain cstring invariant
            return true;
        }
    }
    return false;
}

bool AuthNegotiator::isEndOfLine() const
{
    return m_line.length >= 2 &&
           m_line.begin[m_line.length - 2] == '\r' && m_line.begin[m_line.length - 1] == '\n';
}

void AuthNegotiator::advanceState()
{
    // TODO authentication ping-pong
    // some findings:
    // - the string after the server OK is its UUID that also appears in the address string
    // - the string after the client "EXTERNAL" is the hex-encoded UID

    cout << m_line.begin;
}
