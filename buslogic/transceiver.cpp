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

#include "transceiver.h"

#include "authnegotiator.h"
#include "icompletionclient.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"

#include <iostream>

using namespace std;

Transceiver::Transceiver(IEventDispatcher *dispatcher, const PeerAddress &peer)
   : m_client(0),
     m_receivingMessage(0),
     m_sendSerial(0),
     m_connection(0),
     m_mainThreadTransceiver(0),
     m_authNegotiator(0),
     m_eventDispatcher(0)
{
    m_peerAddress = peer;

    cout << "session bus socket type: " << peer.socketType() << '\n';
    cout << "session bus path: " << peer.path() << '\n';

    m_connection = IConnection::create(peer);
    m_connection->setEventDispatcher(dispatcher);
    cout << "connection is " << (m_connection->isOpen() ? "open" : "closed") << ".\n";
    m_authNegotiator = new AuthNegotiator(m_connection);
    m_authNegotiator->setCompletionClient(this);

    // Announce our presence to the bus and have it send some introductory information of its own
    Message *hello = new Message();
    hello->setType(Message::MethodCallMessage);
    hello->setDestination(std::string("org.freedesktop.DBus"));
    hello->setInterface(std::string("org.freedesktop.DBus"));
    hello->setPath(std::string("/org/freedesktop/DBus"));
    hello->setMethod(std::string("Hello"));
    sendAsync(hello);
}

Transceiver::~Transceiver()
{
    delete m_connection;
    m_connection = 0;
    delete m_authNegotiator;
    m_authNegotiator = 0;
    // TODO delete m_receivingMessage ?
}

Message *Transceiver::sendAndAwaitReply(Message *m)
{
    // TODO!
    return 0;
}

void Transceiver::sendAsync(Message *m)
{
    m->setSerial(++m_sendSerial);
    m_sendQueue.push_back(m);
    m->setCompletionClient(this);
    if (!m_authNegotiator && m_sendQueue.size() == 1) {
        m->writeTo(m_connection);
    }
}

IConnection *Transceiver::connection() const
{
    return m_connection;
}

ITransceiverClient *Transceiver::client() const
{
    return m_client;
}

void Transceiver::setClient(ITransceiverClient *client)
{
    m_client = client;
}

void Transceiver::notifyCompletion(void *task)
{
    if (m_authNegotiator) {
        assert(task == m_authNegotiator);
        delete m_authNegotiator;
        m_authNegotiator = 0;
        // cout << "Authenticated.\n";
        assert(!m_sendQueue.empty()); // the hello message should be in the queue
        m_sendQueue.front()->writeTo(m_connection);
        receiveNextMessage();
    } else {
        if (!m_sendQueue.empty() && task == m_sendQueue.front()) {
            // cout << "Sent message.\n";
            delete m_sendQueue.front();
            m_sendQueue.pop_front();
            if (!m_sendQueue.empty()) {
                m_sendQueue.front()->writeTo(m_connection);
            }
        } else {
            // cout << "Received message.\n";
            assert(task == m_receivingMessage);
            Message *const receivedMessage = m_receivingMessage;
            receiveNextMessage();
            m_client->messageReceived(receivedMessage);
        }
    }
}

void Transceiver::receiveNextMessage()
{
    m_receivingMessage = new Message;
    m_receivingMessage->setCompletionClient(this);
    m_receivingMessage->readFrom(m_connection);
}
