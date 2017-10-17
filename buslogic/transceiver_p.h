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

#include "connectaddress.h"
#include "eventdispatcher_p.h"
#include "icompletionclient.h"
#include "spinlock.h"

#include <deque>
#include <unordered_map>
#include <vector>

class AuthNegotiator;
class HelloReceiver;
class IConnection;
class IMessageReceiver;
class ClientConnectedHandler;

/*
 How to handle destruction of connected Transceivers

 Main thread transceiver destroyed: Need to
 - "cancel" registered PendingReplies from other threads
   (I guess also own ones, we're not doing that, I think...)
 - Make sure that other threads stop calling us because that's going to be a memory error when
   our instance has been deleted

 Secondary thread Transceiver destroyed: Need to
  - "cancel" PendingReplies registered in main thread
  - unregister from main thread as receiver of spontaneous messages because receiving events about
    it is going to be a memory error when our instance has been deleted

 Problem areas:
  - destroying a Transceiver with a locked lock (locked from another thread, obviously)
    - can solved by "thoroughly" disconnecting from everything before destruction
  - deadlocks / locking order - preliminary solution: always main Transceiver first, then secondary
    - what about the lock in EventDispatcher?
  - blocking: secondary blocking (as in waiting for an event - both Transceivers wait on *locks* of
    the other) on main is okay, it does that all the time anyway. main blocking on secondary is
    probably (not sure) not okay.

 Let's define some invariants:
  - When a Transceiver is destroyed, all its PendingReply instances must have been  detached
    (completed with or without error) or destroyed. "Its" means sent through that Transceiver's
    send() method, not when a PendingReply is using the connection of the Transceiver but send()
    was called on the Transceiver of another thread.
  - When a master and a secondary transceiver try to communicate in any way, and the other party
    has been destroyed, communication will fail gracefully and there will be no crash or undefined
    behavior. Any pending replies that cannot finish successfully anymore will finish with an
    LocalDisconnect error.
 */

class TransceiverPrivate : public ICompletionClient
{
public:
    static TransceiverPrivate *get(Transceiver *t) { return t->d; }

    TransceiverPrivate(EventDispatcher *dispatcher);
    void close();

    void authAndHello(Transceiver *parent);
    void handleHelloReply();
    void handleClientConnected();

    uint32 takeNextSerial();

    Error prepareSend(Message *msg);
    void sendPreparedMessage(Message msg);

    void handleCompletion(void *task) override;
    bool maybeDispatchToPendingReply(Message *m);
    void receiveNextMessage();

    void unregisterPendingReply(PendingReplyPrivate *p);
    void cancelAllPendingReplies();
    void discardPendingRepliesForSecondaryThread(TransceiverPrivate *t);

    // For cross-thread communication between thread Transceivers. We could have a more complete event
    // system, but there is currently no need, so keep it simple and limited.
    void processEvent(Event *evt); // called from thread-local EventDispatcher

    enum {
        Unconnected,
        ServerWaitingForClient,
        Authenticating,
        AwaitingUniqueName,
        Connected
    } m_state;

    IMessageReceiver *m_client;
    Message *m_receivingMessage;

    std::deque<Message> m_sendQueue; // waiting to be sent

    // only one of them can be non-null. exception: in the main thread, m_mainThreadTransceiver
    // equals this, so that the main thread knows it's the main thread and not just a thread-local
    // transceiver.
    IConnection *m_connection;

    HelloReceiver *m_helloReceiver;
    ClientConnectedHandler *m_clientConnectedHandler;

    EventDispatcher *m_eventDispatcher;
    ConnectAddress m_connectAddress;
    std::string m_uniqueName;
    AuthNegotiator *m_authNegotiator;

    int m_defaultTimeout;

    class PendingReplyRecord
    {
    public:
        PendingReplyRecord(PendingReplyPrivate *pr) : isForSecondaryThread(false), ptr(pr) {}
        PendingReplyRecord(TransceiverPrivate *tp) : isForSecondaryThread(true), ptr(tp) {}

        PendingReplyPrivate *asPendingReply() const
            { return isForSecondaryThread ? nullptr : static_cast<PendingReplyPrivate *>(ptr); }
        TransceiverPrivate *asTransceiver() const
            { return isForSecondaryThread ? static_cast<TransceiverPrivate *>(ptr) : nullptr; }

    private:
        bool isForSecondaryThread;
        void *ptr;
    };
    std::unordered_map<uint32, PendingReplyRecord> m_pendingReplies; // replies we're waiting for

    Spinlock m_lock; // only one lock because things done with lock held are quick, and anyway you shouldn't
                     // be using one connection from multiple threads if you need best performance

    std::atomic<uint32> m_sendSerial;

    std::unordered_map<TransceiverPrivate *, CommutexPeer> m_secondaryThreadLinks;
    std::vector<CommutexPeer> m_unredeemedCommRefs; // for createCommRef() and the constructor from CommRef

    TransceiverPrivate *m_mainThreadTransceiver;
    CommutexPeer m_mainThreadLink;
};

#endif // TRANSCEIVER_P_H
