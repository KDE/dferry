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
#include "pendingreply.h"
#include "pendingreply_p.h"

#include <cassert>
#include <iostream>

using namespace std;

class HelloReceiver : public ICompletionClient
{
public:
    void notifyCompletion(void *task) override;
    PendingReply m_helloReply; // keep it here so it conveniently goes away when it's done
    Transceiver *m_parent;
};

void HelloReceiver::notifyCompletion(void *)
{
    m_parent->processHello();
}

Transceiver::Transceiver(EventDispatcher *dispatcher, const PeerAddress &peer)
   : m_client(nullptr),
     m_receivingMessage(nullptr),
     m_sendSerial(1),
     m_defaultTimeout(25000),
     m_connection(nullptr),
     m_mainThreadTransceiver(nullptr),
     m_helloReceiver(nullptr),
     m_eventDispatcher(dispatcher),
     m_peerAddress(peer),
     m_authNegotiator(nullptr)
{
    cout << "session bus socket type: " << peer.socketType() << '\n';
    cout << "session bus path: " << peer.path() << '\n';
    m_connection = IConnection::create(peer);
    m_connection->setEventDispatcher(dispatcher);
    cout << "connection is " << (m_connection->isOpen() ? "open" : "closed") << ".\n";
    authAndHello();
}

Transceiver::~Transceiver()
{
    delete m_connection;
    m_connection = nullptr;
    delete m_authNegotiator;
    m_authNegotiator = nullptr;
    delete m_helloReceiver;
    m_helloReceiver = nullptr;
    // TODO delete m_receivingMessage ?
}

void Transceiver::authAndHello()
{
    m_authNegotiator = new AuthNegotiator(m_connection);
    m_authNegotiator->setCompletionClient(this);

    // Announce our presence to the bus and have it send some introductory information of its own
    Message *hello = new Message();
    hello->setType(Message::MethodCallMessage);
    hello->setExpectsReply(false);
    hello->setDestination(std::string("org.freedesktop.DBus"));
    hello->setInterface(std::string("org.freedesktop.DBus"));
    hello->setPath(std::string("/org/freedesktop/DBus"));
    hello->setMethod(std::string("Hello"));

    m_helloReceiver = new HelloReceiver;
    m_helloReceiver->m_helloReply = send(hello);
    m_helloReceiver->m_helloReply.setCompletionClient(m_helloReceiver);
    m_helloReceiver->m_parent = this;
}

void Transceiver::processHello()
{
    assert(m_helloReceiver->m_helloReply.hasNonErrorReply()); // TODO real error handling (more below)
    // ### following line is ugly and slow!! Indicates a need for better API.
    ArgumentList argList = m_helloReceiver->m_helloReply.reply().argumentList();
    delete m_helloReceiver;
    m_helloReceiver = nullptr;
    ArgumentList::Reader reader = argList.beginRead();
    cstring busName = reader.readString();
    assert(reader.state() == ArgumentList::Finished);
    cout << "teh bus name is:" << busName.begin << endl;
}

void Transceiver::setDefaultReplyTimeout(int msecs)
{
    m_defaultTimeout = msecs;
}

int Transceiver::defaultReplyTimeout() const
{
    return m_defaultTimeout;
}

PendingReply Transceiver::send(Message *m, int timeoutMsecs)
{
    if (timeoutMsecs == DefaultTimeout) {
        timeoutMsecs = m_defaultTimeout;
    }

    PendingReplyPrivate *pendingPriv = new PendingReplyPrivate(m_eventDispatcher, timeoutMsecs);
    pendingPriv->m_transceiver = this;
    pendingPriv->m_client = nullptr;
    pendingPriv->m_serial = m_sendSerial;
    m_pendingReplies.emplace(m_sendSerial, pendingPriv);

    sendNoReply(m);

    return PendingReply(pendingPriv);
}

PendingReply::Error Transceiver::sendNoReply(Message *m)
{
    // ### (when not called from send()) warn if sending a message without the noreply flag set?
    //     doing that is wasteful, but might be common. needs investigation.
    m->setSerial(m_sendSerial++);
    m_sendQueue.push_back(m);
    m->setCompletionClient(this);
    if (!m_authNegotiator && m_sendQueue.size() == 1) {
        m->send(m_connection);
    }
    return PendingReply::Error::None;// ###
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
        m_authNegotiator = nullptr;
        // cout << "Authenticated.\n";
        assert(!m_sendQueue.empty()); // the hello message should be in the queue
        m_sendQueue.front()->send(m_connection);
        receiveNextMessage();
    } else {
        if (!m_sendQueue.empty() && task == m_sendQueue.front()) {
            // cout << "Sent message.\n";
            delete m_sendQueue.front();
            m_sendQueue.pop_front();
            if (!m_sendQueue.empty()) {
                m_sendQueue.front()->send(m_connection);
            }
        } else {
            assert(task == m_receivingMessage);
            Message *const receivedMessage = m_receivingMessage;
            cout << "Received message; type:" << receivedMessage->type() << "\n";

            receiveNextMessage();

            bool replyDispatched = false;

            if (receivedMessage->type() == Message::MethodReturnMessage ||
                receivedMessage->type() == Message::ErrorMessage) {
                uint32 rs = receivedMessage->replySerial();
                cout << "Received message: looking for pending reply for serial " << rs << "\n";
                auto it = m_pendingReplies.find(rs);
                if (it != m_pendingReplies.end()) {
                    replyDispatched = true;
                    PendingReplyPrivate *pr = it->second;
                    m_pendingReplies.erase(it);

                    cout << "Received message: dispatching to PendingReply.\n";
                    cout << "Received message: argumentList A " << receivedMessage->argumentList().prettyPrint() << '\n';
                    pr->m_reply = *receivedMessage;
                    cout << "Received message: argumentList B " << pr->m_reply.argumentList().prettyPrint() << '\n';
                    pr->notifyDone();
                }
            }

            if (!replyDispatched) {
                if (m_client) {
                    cout << "Received message: dispatching to catch-all.\n";
                    m_client->messageReceived(receivedMessage);
                } else {
                    cerr << "warning, dropping message on the floor because no client is registered.\n";
                    delete receivedMessage;
                }
            }

        }
    }
}

void Transceiver::receiveNextMessage()
{
    m_receivingMessage = new Message;
    m_receivingMessage->setCompletionClient(this);
    m_receivingMessage->receive(m_connection);
}

void Transceiver::unregisterPendingReply(PendingReplyPrivate *p)
{
#ifndef NDEBUG
    auto it = m_pendingReplies.find(p->m_serial);
    assert(it != m_pendingReplies.end());
    assert(it->second == p);
#endif
    m_pendingReplies.erase(p->m_serial);
}
