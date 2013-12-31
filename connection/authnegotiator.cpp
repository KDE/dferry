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

#include "authnegotiator.h"

#include "icompletionclient.h"
#include "iconnection.h"
#include "pathfinder.h"
#include "stringtools.h"

#include <cassert>
#include <iostream>
#include <sstream>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

AuthNegotiator::AuthNegotiator(IConnection *connection)
   : m_state(InitialState),
     m_completionClient(0)
{
    connection->addClient(this);
    setIsReadNotificationEnabled(true);
    byte nullBuf[1] = { 0 };
    connection->write(chunk(nullBuf, 1));

    // no idea why the uid is first encoded to ascii and the ascii to hex...
    uid_t uid = geteuid();
    stringstream uidDecimal;
    uidDecimal << uid;
    string extLine = "AUTH EXTERNAL " + hexEncode(uidDecimal.str()) + "\r\n";
    cout << extLine;
    connection->write(chunk(extLine.c_str(), extLine.length()));
    m_state = ExpectOkState;
}

bool AuthNegotiator::isFinished() const
{
    return m_state >= AuthenticationFailedState;
}

bool AuthNegotiator::isAuthenticated() const
{
    return m_state == AuthenticatedState;
}

void AuthNegotiator::setCompletionClient(ICompletionClient *client)
{
    m_completionClient = client;
}

void AuthNegotiator::notifyConnectionReadyRead()
{
    bool wasFinished = isFinished();
    while (!isFinished() && readLine()) {
        advanceState();
    }
    if (isFinished() && !wasFinished && m_completionClient) {
        m_completionClient->notifyCompletion(this);
    }
}

bool AuthNegotiator::readLine()
{
    // don't care about performance here, this doesn't run often or process much data
    if (isEndOfLine()) {
        m_line.clear(); // start a new line
    }
    while (connection()->availableBytesForReading()) {
        byte readBuf[1];
        chunk in = connection()->read(readBuf, 1);
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

    cout << "> " << m_line;

    switch (m_state) {
    case ExpectOkState: {
        // TODO check the OK
        cstring negotiateLine("NEGOTIATE_UNIX_FD\r\n");
        cout << negotiateLine.begin;
        connection()->write(chunk(negotiateLine.begin, negotiateLine.length));
        m_state = ExpectUnixFdResponseState;
        break; }
    case ExpectUnixFdResponseState: {
        // TODO check the response
        cstring beginLine("BEGIN\r\n");
        cout << beginLine.begin;
        connection()->write(chunk(beginLine.begin, beginLine.length));
        m_state = AuthenticatedState;
        break; }
    default:
        m_state = AuthenticationFailedState;
        connection()->close();
    }
}
