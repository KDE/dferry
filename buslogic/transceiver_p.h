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

#ifndef TRANSCEIVER_P_H
#define TRANSCEIVER_P_H

#include "transceiver.h"

#include "icompletionclient.h"

#include <deque>
#include <unordered_map>

class AuthNegotiator;
class HelloReceiver;

class TransceiverPrivate : public ICompletionClient
{
public:
    static TransceiverPrivate *get(Transceiver *t) { return t->d; }

    TransceiverPrivate(EventDispatcher *dispatcher, const PeerAddress &peer);

    void authAndHello(Transceiver *parent);
    void processHello();
    void enqueueSendFromOtherThread(Message *m);
    void addReplyFromOtherThread(Message *m);
    void notifyCompletion(void *task) override;

    void receiveNextMessage();

    void unregisterPendingReply(PendingReplyPrivate *p);

    ITransceiverClient *m_client;
    Message *m_receivingMessage;
    int m_sendSerial; // TODO handle recycling of serials
    int m_defaultTimeout;
    std::deque<Message *> m_sendQueue; // waiting to be sent
    std::unordered_map<uint32, PendingReplyPrivate *> m_pendingReplies; // replies we're waiting for

    // only one of them can be non-null. exception: in the main thread, m_mainThreadTransceiver
    // equals this, so that the main thread knows it's the main thread and not just a thread-local
    // transceiver.
    IConnection *m_connection;
    Transceiver *m_mainThreadTransceiver;

    HelloReceiver *m_helloReceiver;

    EventDispatcher *m_eventDispatcher;
    PeerAddress m_peerAddress;
    AuthNegotiator *m_authNegotiator;
};

#endif // TRANSCEIVER_P_H
