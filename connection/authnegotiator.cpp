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
     m_state(InitialState)
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
    m_state = ExpectOkState;
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
        m_line.clear(); // start a new line
    }
    while (m_connection->availableBytesForReading()) {
        byte readBuf[1];
        array in = m_connection->read(readBuf, 1);
        assert(in.length == 1);
        m_line += in.begin[0];

        if (isEndOfLine()) {
            return true;
        }
    }
    return false;
}

bool AuthNegotiator::isEndOfLine() const
{
    return m_line.length() >= 2 &&
           m_line[m_line.length() - 2] == '\r' && m_line[m_line.length() - 1] == '\n';
}

void AuthNegotiator::advanceState()
{
    // TODO authentication ping-pong
    // some findings:
    // - the string after the server OK is its UUID that also appears in the address string

    cout << m_line;

    switch (m_state) {
    case ExpectOkState: {
        // TODO check the OK
        cstring negotiateLine("NEGOTIATE_UNIX_FD\r\n");
        cout << negotiateLine.begin;
        m_connection->write(array(negotiateLine.begin, negotiateLine.length));
        m_state = ExpectUnixFdResponseState;
        break; }
    case ExpectUnixFdResponseState: {
        // TODO check the response
        cstring beginLine("BEGIN\r\n");
        cout << beginLine.begin;
        m_connection->write(array(beginLine.begin, beginLine.length));
        break; }
    default:
        m_state = AuthenticationFailedState;
        m_connection->close();
    }
}
