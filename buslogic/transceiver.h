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

#ifndef TRANSCEIVER_H
#define TRANSCEIVER_H

#include "icompletionclient.h"

#include <deque>

/* Transceiver must handle queueing of messages and shuffle messages that go through
 * a different thread's connection (MainConnection) to the right thread's Transceiver.
 * For example, an auxiliary thread calls sendAndAwaitReply(). While the aux thread is waiting,
 * the message must be picked up by the main thread in its event dispatcher, it must wait for
 * the reply and then forward it back to the aux thread.
 * In case the connection is in the current thread, it will simply send and block.
 *
 * When sendAsync() is used, the same things minus the blocking happen.
 *
 * Question1: Does sendAndAwaitReply() send its argument right away, or enqueue it and send
 * messages in order? The former seems less likely to cause "unplanned behavior", the latter might
 * deadlock more easily.
 * I think the second option is preferable because changing the order of calls is Evil.
 *
 * Question 2: How to detect deadlocks?
 * (maybe have a timeout before the full 20(?) seconds timeout where we look for suspicious
 * state, a certain pattern in the send and receive queues or some such)
 */

class AuthNegotiator;
class IConnection;
class IEventDispatcher;
class ITransceiverClient;
class Message;

class Transceiver : public ICompletionClient
{
public:
    enum ThreadAffinity
    {
        MainConnection = 0,
        ThreadLocalConnection
    };

    // TODO Transceiver(IEventDispatcher *dispatcher, const PeerAddress &peer, enum ThreadAffinity);
    // this sets up a connection ready to use
    Transceiver(IEventDispatcher *dispatcher);
    ~Transceiver();

    Message *sendAndAwaitReply(Message *m);
    void sendAsync(Message *m);

    IConnection *connection() const; // probably only needed for debugging

    ITransceiverClient *client() const;
    void setClient(ITransceiverClient *client);

private:
    void enqueueSendFromOtherThread(Message *m);
    void addReplyFromOtherThread(Message *m);
    virtual void notifyCompletion(void *task);

    void receiveNextMessage();

    ITransceiverClient *m_client;
    Message *m_receivingMessage;
    int m_sendSerial; // TODO handle recycling of serials
    std::deque<Message *> m_sendQueue; // waiting to be sent
    std::deque<Message *> m_receiveQueue; // waiting for event loop to run and notify the receiver

    // only one of them can be non-null. exception: in the main thread, m_mainThreadTransceiver
    // equals this, so that the main thread knows it's the main thread and not just a thread-local
    // transceiver.
    IConnection *m_connection;
    Transceiver *m_mainThreadTransceiver;

    AuthNegotiator *m_authNegotiator;
    IEventDispatcher *m_eventDispatcher;
};

#endif // TRANSCEIVER_H
