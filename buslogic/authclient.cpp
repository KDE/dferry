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

#include "authclient.h"

#include "icompletionclient.h"
#include "iconnection.h"
#include "stringtools.h"

#include <cassert>
#include <iostream>
#include <sstream>

#ifdef __unix__
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include "winutil.h"
#endif

using namespace std;

AuthClient::AuthClient(IConnection *connection)
   : m_state(InitialState),
     m_completionClient(nullptr)
{
    cerr << "AuthClient constructing\n";
    connection->addClient(this);
    setReadNotificationEnabled(true);
    byte nullBuf[1] = { 0 };
    connection->write(chunk(nullBuf, 1));

    stringstream uidEncoded;
#ifdef _WIN32
    // Most common (or rather... actually used) authentication method on Windows:
    // - Server publishes address of a nonce file; the file name is in a shared memory segment
    // - Client reads nonce file
    // - Client connects and sends the nonce data, TODO: before or after the null byte / is there a null byte?
    // - Client uses EXTERNAL auth and says which Windows security ID (SID) it intends to have
    uidEncoded << fetchWindowsSid();
#else
    // Most common (or rather... actually used) authentication method on Unix derivatives:
    // - Client sends a null byte so the server has something to receive with recvmsg()
    // - Server checks UID using SCM_CREDENTIALS, a mechanism of Unix local sockets
    // - Client uses EXTERNAL auth and says which Unix user ID it intends to have

    // The numeric UID is first encoded to ASCII ("1000") and the ASCII to hex... because.
    uidEncoded << geteuid();
#endif
    string extLine = "AUTH EXTERNAL " + hexEncode(uidEncoded.str()) + "\r\n";
    cout << extLine;
    connection->write(chunk(extLine.c_str(), extLine.length()));
    m_state = ExpectOkState;
}

bool AuthClient::isFinished() const
{
    return m_state >= AuthenticationFailedState;
}

bool AuthClient::isAuthenticated() const
{
    return m_state == AuthenticatedState;
}

void AuthClient::setCompletionClient(ICompletionClient *client)
{
    m_completionClient = client;
}

void AuthClient::handleConnectionCanRead()
{
    bool wasFinished = isFinished();
    while (!isFinished() && readLine()) {
        advanceState();
    }
    if (isFinished() && !wasFinished && m_completionClient) {
        m_completionClient->handleCompletion(this);
    }
}

bool AuthClient::readLine()
{
    // don't care about performance here, this doesn't run often or process much data
    if (isEndOfLine()) {
        m_line.clear(); // start a new line
    }
    while (connection()->availableBytesForReading()) {
        byte readBuf[1];
        chunk in = connection()->read(readBuf, 1);
        assert(in.length == 1);
        m_line += char(in.ptr[0]);

        if (isEndOfLine()) {
            return true;
        }
    }
    return false;
}

bool AuthClient::isEndOfLine() const
{
    return m_line.length() >= 2 &&
           m_line[m_line.length() - 2] == '\r' && m_line[m_line.length() - 1] == '\n';
}

void AuthClient::advanceState()
{
    // TODO authentication ping-pong done *properly* (grammar / some simple state machine),
    //      but hey, this works for now!
    // some findings:
    // - the string after the server OK is its UUID that also appears in the address string

    cout << "> " << m_line;

    switch (m_state) {
    case ExpectOkState: {
        // TODO check the OK
#ifdef __unix__
        cstring negotiateLine("NEGOTIATE_UNIX_FD\r\n");
        cout << negotiateLine.ptr;
        connection()->write(chunk(negotiateLine.ptr, negotiateLine.length));
        m_state = ExpectUnixFdResponseState;
        break; }
    case ExpectUnixFdResponseState: {
#endif
        // TODO check the response
        cstring beginLine("BEGIN\r\n");
        cout << beginLine.ptr;
        connection()->write(chunk(beginLine.ptr, beginLine.length));
        m_state = AuthenticatedState;
        break; }
    default:
        m_state = AuthenticationFailedState;
        connection()->close();
    }
}
