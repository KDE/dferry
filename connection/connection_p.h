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

#ifndef CONNECTION_P_H
#define CONNECTION_P_H

#include "connection.h"

#include "connectaddress.h"
#include "eventdispatcher_p.h"
#include "icompletionlistener.h"
#include "iioeventforwarder.h"
#include "spinlock.h"

#include <deque>
#include <unordered_map>
#include <vector>

class AuthClient;
class HelloReceiver;
class IMessageReceiver;
class ITransport;
class ClientConnectedHandler;

/*
 How to handle destruction of connected Connections

 Main thread connection destroyed: Need to
 - "cancel" registered PendingReplies from other threads
   (I guess also own ones, we're not doing that, I think...)
 - Make sure that other threads stop calling us because that's going to be a memory error when
   our instance has been deleted

 Secondary thread Connection destroyed: Need to
  - "cancel" PendingReplies registered in main thread
  - unregister from main thread as receiver of spontaneous messages because receiving events about
    it is going to be a memory error when our instance has been deleted

 Problem areas:
  - destroying a Connection with a locked lock (locked from another thread, obviously)
    - can solved by "thoroughly" disconnecting from everything before destruction
  - deadlocks / locking order - preliminary solution: always main Connection first, then secondary
    - what about the lock in EventDispatcher?
  - blocking: secondary blocking (as in waiting for an event - both Connections wait on *locks* of
    the other) on main is okay, it does that all the time anyway. main blocking on secondary is
    probably (not sure) not okay.

 Let's define some invariants:
  - When a Connection is destroyed, all its PendingReply instances must have been  detached
    (completed with or without error) or destroyed. "Its" means sent through that Connection's
    send() method, not when a PendingReply is using the connection of the Connection but send()
    was called on the Connection of another thread.
  - When a master and a secondary connection try to communicate in any way, and the other party
    has been destroyed, communication will fail gracefully and there will be no crash or undefined
    behavior. Any pending replies that cannot finish successfully anymore will finish with an
    LocalDisconnect error.
*/

class ConnectionStateChanger;

// This class sits between EventDispatcher and ITransport for I/O event forwarding purposes,
// which is why it is both a listener (for EventDispatcher) and a source (mainly for ITransport)
class ConnectionPrivate : public IIoEventForwarder, public ICompletionListener
{
public:
    enum State {
        Unconnected = 0,
        ServerWaitingForClient,
        Authenticating,
        AwaitingUniqueName,
        Connected
    };

    static ConnectionPrivate *get(Connection *c) { return c->d; }

    ConnectionPrivate(Connection *connection, EventDispatcher *dispatcher);
    void close(Error withError);

    // from IIOEventForwarder
    IO::Status handleIoReady(IO::RW rw) override;

    void startAuthentication();
    void handleHelloReply();
    void handleHelloFailed();
    // invokes m_connectionStateListener, if any
    void notifyStateChange(Connection::State oldUserState, Connection::State newUserState);

    void handleClientConnected();

    uint32 takeNextSerial();

    Error prepareSend(Message *msg);
    void sendPreparedMessage(Message msg);

    void handleCompletion(void *task) override;
    bool maybeDispatchToPendingReply(Message *m);
    bool maybeDispatchToPendingReply(uint32 serial, Error error);
    void receiveNextMessage();

    void unregisterPendingReply(PendingReplyPrivate *p);
    void cancelAllPendingReplies(Error withError);
    void discardPendingRepliesForSecondaryThread(ConnectionPrivate *t);

    // For cross-thread communication between thread Connections. We could have a more complete event
    // system, but there is currently no need, so keep it simple and limited.
    void processEvent(Event *evt); // called from thread-local EventDispatcher

    State m_state = Unconnected;
    bool m_closing = false;
    bool m_unixFdPassingEnabled = false;

    Connection *m_connection = nullptr;
    IMessageReceiver *m_client = nullptr;
    IConnectionStateListener *m_connectionStateListener = nullptr;

    Message *m_receivingMessage = nullptr;
    std::deque<Message> m_sendQueue; // waiting to be sent

    // only one of them can be non-null. exception: in the main thread, m_mainThreadConnection
    // equals this, so that the main thread knows it's the main thread and not just a thread-local
    // connection.
    ITransport *m_transport = nullptr;

    HelloReceiver *m_helloReceiver = nullptr;
    ClientConnectedHandler *m_clientConnectedHandler = nullptr;

    EventDispatcher *m_eventDispatcher = nullptr;
    ConnectAddress m_connectAddress;
    std::string m_uniqueName;
    AuthClient *m_authClient = nullptr;

    int m_defaultTimeout = 25000;

    class PendingReplyRecord
    {
    public:
        PendingReplyRecord(PendingReplyPrivate *pr) : isForSecondaryThread(false), ptr(pr) {}
        PendingReplyRecord(ConnectionPrivate *tp) : isForSecondaryThread(true), ptr(tp) {}

        PendingReplyPrivate *asPendingReply() const
            { return isForSecondaryThread ? nullptr : static_cast<PendingReplyPrivate *>(ptr); }
        ConnectionPrivate *asConnection() const
            { return isForSecondaryThread ? static_cast<ConnectionPrivate *>(ptr) : nullptr; }

    private:
        bool isForSecondaryThread;
        void *ptr;
    };
    std::unordered_map<uint32, PendingReplyRecord> m_pendingReplies; // replies we're waiting for

    Spinlock m_lock; // only one lock because things done with lock held are quick, and anyway you shouldn't
                     // be using one connection from multiple threads if you need best performance

    std::atomic<uint32> m_sendSerial { 1 };

    std::unordered_map<ConnectionPrivate *, CommutexPeer> m_secondaryThreadLinks;
    std::vector<CommutexPeer> m_unredeemedCommRefs; // for createCommRef() and the constructor from CommRef

    ConnectionPrivate *m_mainThreadConnection = nullptr;
    CommutexPeer m_mainThreadLink;
};

// This class helps with notifying a Connection's StateChanegListener when connection state changes.
// Its benefits are:
// - Tracks state changes in a few easily verified pieces of code
// - Prevents crashes from the  following scenario: IConnectionStateListener is notified about a change. As a
//   reaction, it may delete the Connection. The listener returns and control passes back into Connection
//   code. Connection code touches some of its (or rather, ConnectionPrivate's) data, which has been deleted
//   at that point. Undefined behavior ensues.
//   With the help of this class, the IConnectionStateListener is always called just before exit, so that no
//   member data can be touched afterwards. (This pattern is a good idea for almost any kind of callback.)
class ConnectionStateChanger
{
public:
    ConnectionStateChanger(ConnectionPrivate *cp);
    ConnectionStateChanger(ConnectionPrivate *cp, ConnectionPrivate::State newState);

    ConnectionStateChanger(const ConnectionStateChanger &) = delete;
    ConnectionStateChanger &operator=(const ConnectionStateChanger &) = delete;

    ~ConnectionStateChanger();

    void setNewState(ConnectionPrivate::State newState);
    void disable();

private:
    ConnectionPrivate *m_connPrivate;
    int32 m_oldState = -1; // either -1 or a valid ConnectionPrivate::State
};

#endif // CONNECTION_P_H
