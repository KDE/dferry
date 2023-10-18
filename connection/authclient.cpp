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

#include "icompletionlistener.h"
#include "itransport.h"
#include "stringtools.h"

#include <cassert>
#include <cstring>
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

AuthClient::AuthClient(ITransport *transport)
   : m_state(InitialState),
     m_nextAuthMethod(0),
     m_fdPassingEnabled(false),
     m_completionListener(nullptr)
{
    transport->setReadListener(this);
    byte nullBuf[1] = { 0 };
    transport->write(chunk(nullBuf, 1));

    sendNextAuthMethod();
}

bool AuthClient::isFinished() const
{
    return m_state >= AuthenticationFailedState;
}

bool AuthClient::isAuthenticated() const
{
    return m_state == AuthenticatedState;
}

bool AuthClient::isUnixFdPassingEnabled() const
{
    return m_fdPassingEnabled;
}

void AuthClient::setCompletionListener(ICompletionListener *listener)
{
    m_completionListener = listener;
}

IO::Status AuthClient::handleTransportCanRead()
{
    const bool wasFinished = isFinished();
    while (!isFinished() && readLine()) {
        advanceState();
    }
    if (!readTransport()->isOpen()) {
        m_state = AuthenticationFailedState;
        return IO::Status::RemoteClosed;
    }
    if (isFinished() && !wasFinished && m_completionListener) {
        m_completionListener->handleCompletion(this);
    }
    return IO::Status::OK;
}

bool AuthClient::readLine()
{
    // don't care about performance here, this doesn't run often or process much data
    if (isEndOfLine()) {
        m_line.clear(); // start a new line
    }
    while (readTransport()->availableBytesForReading()) {
        byte readBuf[1];
        const IO::Result iores = readTransport()->read(readBuf, 1);
        if (iores.length != 1 || iores.status != IO::Status::OK) {
            return false;
        }
        m_line += char(readBuf[0]);

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

void AuthClient::sendNextAuthMethod()
{
    switch (m_nextAuthMethod) {
    case AuthExternal: {
        std::stringstream uidEncoded;
#ifdef _WIN32
        uidEncoded << fetchWindowsSid();
#else
        // The numeric UID is first encoded to ASCII ("1000") and the ASCII to hex... because.
        uidEncoded << geteuid();
#endif
        std::string extLine = "AUTH EXTERNAL " + hexEncode(uidEncoded.str()) + "\r\n";
        readTransport()->write(chunk(extLine.c_str(), extLine.length()));

        m_nextAuthMethod++;
        m_state = ExpectOkState;
        break;}
    case AuthAnonymous: {
        // This is basically "trust me bro" auth. The server must be configured to accept this, obviously.
        // The part after ANONYMOUS seems arbitrary, we send a hex-encoded "dferry". libdbus-1 seems to send
        // something like a hex-encoded "libdbus 1.14.10".

        cstring anonLine("AUTH ANONYMOUS 646665727279\r\n");
        readTransport()->write(chunk(anonLine.ptr, anonLine.length));

        m_nextAuthMethod++;
        m_state = ExpectOkState;
        break; }
    default:
        m_state = AuthenticationFailedState;
        break;
    }
}

void AuthClient::advanceState()
{
    // Note: since the connection is new and send buffers are typically several megabytes, there is basically
    // no chance that writing will block or write only partial data. Therefore we simplify things by doing
    // write synchronously, which means that we don't need to register as write readiness listener.
    // Therefore, writeTransport() is nullptr, so we have to do the unintuitive readTransport()->write().

    // some findings:
    // - the string after the server OK is its UUID that also appears in the address string

    switch (m_state) {
    case ExpectOkState: {
        const bool authOk = m_line.substr(0, strlen("OK ")) == "OK ";
        if (authOk) {
            // continue below, either negotiate FD passing or just send BEGIN
        } else {
            const bool rejected = m_line.substr(0, strlen("REJECTED")) == "REJECTED";
            if (rejected) {
                m_state = ExpectOkState;
                // TODO read possible authentication methods from REJECTED [space separated list of methods]
                sendNextAuthMethod();
            } else {
                m_state = AuthenticationFailedState; // protocol violation -> we're out
            }
            break;
        }
#ifdef __unix__
        if (readTransport()->supportedPassingUnixFdsCount() > 0) {
            cstring negotiateLine("NEGOTIATE_UNIX_FD\r\n");
            readTransport()->write(chunk(negotiateLine.ptr, negotiateLine.length));
            m_state = ExpectUnixFdResponseState;
            break;
        }
        }
        // fall through
    case ExpectUnixFdResponseState: {
        m_fdPassingEnabled = m_line == "AGREE_UNIX_FD\r\n";
#endif
        cstring beginLine("BEGIN\r\n");
        readTransport()->write(chunk(beginLine.ptr, beginLine.length));
        m_state = AuthenticatedState;
        break; }
    default:
        m_state = AuthenticationFailedState;
        readTransport()->close();
        break;
    }
}
