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

#include "connection.h"
#include "connection_p.h"

#include "arguments.h"
#include "authclient.h"
#include "event.h"
#include "eventdispatcher_p.h"
#include "icompletionlistener.h"
#include "iconnectionstatelistener.h"
#include "imessagereceiver.h"
#include "iserver.h"
#include "localsocket.h"
#include "message.h"
#include "message_p.h"
#include "pendingreply.h"
#include "pendingreply_p.h"
#include "stringtools.h"

#include <algorithm>
#include <cassert>

class HelloReceiver : public IMessageReceiver
{
public:
    void handlePendingReplyFinished(PendingReply *pr, Connection *) override
    {
        assert(pr == &m_helloReply);
        (void) pr;
        m_parent->handleHelloReply();
    }

    PendingReply m_helloReply; // keep it here so it conveniently goes away when it's done
    ConnectionPrivate *m_parent;
};

class ClientConnectedHandler : public ICompletionListener
{
public:
    ~ClientConnectedHandler() override
    {
        delete m_server;
    }

    void handleCompletion(void *) override
    {
        m_parent->handleClientConnected();
    }

    IServer *m_server;
    ConnectionPrivate *m_parent;
};

static Connection::State userState(ConnectionPrivate::State ps)
{
    switch (ps) {
    case ConnectionPrivate::Unconnected:
        return Connection::Unconnected;
    case ConnectionPrivate::ServerWaitingForClient:
    case ConnectionPrivate::Authenticating:
    case ConnectionPrivate::AwaitingUniqueName:
        return Connection::Connecting;
    case ConnectionPrivate::Connected:
        return Connection::Connected;
    }
    assert(false);
    return Connection::Unconnected;
}

ConnectionStateChanger::ConnectionStateChanger(ConnectionPrivate *cp)
   : m_connPrivate(cp)
{
}

ConnectionStateChanger::ConnectionStateChanger(ConnectionPrivate *cp, ConnectionPrivate::State newState)
   : m_connPrivate(cp),
     m_oldState(cp->m_state)
{
    cp->m_state = newState;
}

ConnectionStateChanger::~ConnectionStateChanger()
{
    if (m_oldState < 0) {
        return;
    }
    const Connection::State oldUserState = userState(static_cast<ConnectionPrivate::State>(m_oldState));
    const Connection::State newUserState = userState(m_connPrivate->m_state);
    if (oldUserState != newUserState) {
        m_connPrivate->notifyStateChange(oldUserState, newUserState);
    }
}

void ConnectionStateChanger::setNewState(ConnectionPrivate::State newState)
{
    // Ensure that, in the destructor, the new state is always compared to the original old state
    if (m_oldState < 0) {
        m_oldState = m_connPrivate->m_state;
    }
    m_connPrivate->m_state = newState;
}

void ConnectionStateChanger::disable()
{
    m_oldState = -1;
}

ConnectionPrivate::ConnectionPrivate(Connection *connection, EventDispatcher *dispatcher)
   : IIoEventForwarder(EventDispatcherPrivate::get(dispatcher)),
     m_connection(connection),
     m_eventDispatcher(dispatcher)
{
}

IO::Status ConnectionPrivate::handleIoReady(IO::RW rw)
{
    IO::Status status;
    IIoEventListener *const downstream = downstreamListener();
    if (m_state == ServerWaitingForClient) {
        assert(downstream == m_clientConnectedHandler->m_server);
    } else {
        assert(downstream == m_transport);
    }
    if (downstream) {
        status = downstream->handleIoReady(rw);
    } else {
        status = IO::Status::InternalError;
    }

    if (status != IO::Status::OK) {
        if (status != IO::Status::PayloadError) {
            ConnectionStateChanger stateChanger(this);
            stateChanger.setNewState(ConnectionPrivate::Unconnected);
            close(Error::RemoteDisconnect);
        } else {
            assert(!m_sendQueue.empty());
            const Message &msg = m_sendQueue.front();
            uint32 failedSerial = msg.serial();
            Error error = msg.error();
            m_sendQueue.pop_front();
            // If the following fails, there is no "spontaneously failed to send" notification mechanism.
            // It is not a mistake in this case that it fails silently.
            maybeDispatchToPendingReply(failedSerial, error);
        }
    }
    return status;
}

Connection::Connection(EventDispatcher *dispatcher, const ConnectAddress &ca)
   : d(new ConnectionPrivate(this, dispatcher))
{
    d->m_connectAddress = ca;
    assert(d->m_eventDispatcher);
    EventDispatcherPrivate::get(d->m_eventDispatcher)->m_connectionToNotify = d;

    if (ca.type() == ConnectAddress::Type::None || ca.role() == ConnectAddress::Role::None) {
        return;
    }

    ConnectionStateChanger stateChanger(d);

    if (ca.role() == ConnectAddress::Role::PeerServer) {
        // this sets up a server that will be destroyed after accepting exactly one connection
        d->m_clientConnectedHandler = new ClientConnectedHandler;
        ConnectAddress dummyClientAddress;
        IServer *const is = IServer::create(ca, &dummyClientAddress);
        if (is && is->isListening()) {
            d->addIoListener(is);
            is->setNewConnectionListener(d->m_clientConnectedHandler);
            d->m_clientConnectedHandler->m_server = is;
            d->m_clientConnectedHandler->m_parent = d;

            stateChanger.setNewState(ConnectionPrivate::ServerWaitingForClient);
        } else {
            delete is;
        }
    } else {
        d->m_transport = ITransport::create(ca);
        if (d->m_transport && d->m_transport->isOpen()) {
            d->addIoListener(d->m_transport);
            if (ca.role() == ConnectAddress::Role::BusClient) {
                d->startAuthentication();
                stateChanger.setNewState(ConnectionPrivate::Authenticating);
            } else {
                assert(ca.role() == ConnectAddress::Role::PeerClient);
                // get ready to receive messages right away
                d->receiveNextMessage();
                stateChanger.setNewState(ConnectionPrivate::Connected);
            }
        } else {
            delete d->m_transport;
            d->m_transport = nullptr;
        }
    }
}

Connection::Connection(EventDispatcher *dispatcher, CommRef mainConnectionRef)
   : d(new ConnectionPrivate(this, dispatcher))
{
    EventDispatcherPrivate::get(d->m_eventDispatcher)->m_connectionToNotify = d;

    // This must be destroyed after all the Lockers so we notify with no locks held!
    ConnectionStateChanger stateChanger(d);

    d->m_mainThreadLink = std::move(mainConnectionRef.commutex);
    CommutexLocker locker(&d->m_mainThreadLink);
    assert(locker.hasLock());
    Commutex *const id = d->m_mainThreadLink.id();
    if (!id) {
        assert(false);
        return; // stay in Unconnected state
    }

    d->m_mainThreadConnection = mainConnectionRef.connection;
    ConnectionPrivate *mainD = d->m_mainThreadConnection;

    // get the current values - if we got them from e.g. the CommRef they could be outdated
    // and we don't want to wait for more event ping-pong
    SpinLocker mainLocker(&mainD->m_lock);
    d->m_connectAddress = mainD->m_connectAddress;

    // register with the main Connection
    SecondaryConnectionConnectEvent *evt = new SecondaryConnectionConnectEvent();
    evt->connection = d;
    evt->id = id;
    EventDispatcherPrivate::get(mainD->m_eventDispatcher)
                                ->queueEvent(std::unique_ptr<Event>(evt));
    stateChanger.setNewState(ConnectionPrivate::AwaitingUniqueName);
}

Connection::Connection(ITransport *transport, EventDispatcher *ed, const ConnectAddress &address)
   : d(new ConnectionPrivate(this, ed))
{
    // TODO FULLY validate address, also in the other constructors and in ITransport::create()
    //      and in IServer::create()!
    assert(address.role() == ConnectAddress::Role::PeerServer);
    assert(d->m_eventDispatcher);
    d->m_transport = transport;
    d->addIoListener(d->m_transport);
    d->m_connectAddress = address;
    EventDispatcherPrivate::get(d->m_eventDispatcher)->m_connectionToNotify = d;

#if 0
    // TODO make the client authenticate itself, roughly along these lines
    //      (not yet investigated whether peer auth is out of spec, optional or mandatory)
    // this sets up a server that will be destroyed after accepting exactly one connection
    d->m_clientConnectedHandler = new ClientConnectedHandler;
    d->m_clientConnectedHandler->m_server = IServer::create(ca);
    d->m_clientConnectedHandler->m_server->setEventDispatcher(dispatcher);
    d->m_clientConnectedHandler->m_server->setNewConnectionListener(d->m_clientConnectedHandler);
    d->m_clientConnectedHandler->m_parent = d;
#endif
    d->receiveNextMessage();
    ConnectionStateChanger stateChanger(d, ConnectionPrivate::Connected);
}

Connection::Connection(Connection &&other)
{
    d = other.d;
    other.d = nullptr;
    if (d) {
        d->m_connection = this;
    }
}

Connection &Connection::operator=(Connection &&other)
{
    this->~Connection();
    d = other.d;
    other.d = nullptr;
    if (d) {
        d->m_connection = this;
    }
    return *this;
}

Connection::~Connection()
{
    if (!d) {
        return;
    }
    d->close(Error::LocalDisconnect);

    delete d->m_transport;
    delete d->m_authClient;
    delete d->m_helloReceiver;
    delete d->m_receivingMessage;

    delete d;
    d = nullptr;
}

Connection::State Connection::state() const
{
    return userState(d->m_state);
}

void Connection::close()
{
    d->close(Error::LocalDisconnect);
}

void ConnectionPrivate::close(Error withError)
{
    // Can't be main and secondary at the main time - it could be made to work, but what for?
    assert(m_secondaryThreadLinks.empty() || !m_mainThreadConnection);

    if (m_mainThreadConnection) {
        CommutexUnlinker unlinker(&m_mainThreadLink);
        if (unlinker.hasLock()) {
            SecondaryConnectionDisconnectEvent *evt = new SecondaryConnectionDisconnectEvent();
            evt->connection = this;
            EventDispatcherPrivate::get(m_mainThreadConnection->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(evt));
        }
    }

    // Destroy whatever is suitable and available at a given time, in order to avoid things like
    // one secondary thread blocking another indefinitely and smaller dependency-related slowdowns.
    while (!m_secondaryThreadLinks.empty()) {
        for (auto it = m_secondaryThreadLinks.begin(); it != m_secondaryThreadLinks.end(); ) {

            CommutexUnlinker unlinker(&it->second, false);
            if (unlinker.willSucceed()) {
                if (unlinker.hasLock()) {
                    MainConnectionDisconnectEvent *evt = new MainConnectionDisconnectEvent();
                    evt->error = withError;
                    EventDispatcherPrivate::get(it->first->m_eventDispatcher)
                        ->queueEvent(std::unique_ptr<Event>(evt));
                }
                unlinker.unlinkNow(); // don't access the element after erasing it, finish it now
                it = m_secondaryThreadLinks.erase(it);
            } else {
                ++it; // don't block, try again next iteration
            }
        }
    }

    cancelAllPendingReplies(withError);

    EventDispatcherPrivate::get(m_eventDispatcher)->m_connectionToNotify = nullptr;
    if (m_transport) {
        m_transport->close();
    }
    ConnectionStateChanger stateChanger(this, Unconnected);
}

void ConnectionPrivate::startAuthentication()
{
    // Reserve serial 1 for the "hello" message - technically not necessary, there is no required ordering
    // of serials.
    takeNextSerial();
    m_authClient = new AuthClient(m_transport);
    m_authClient->setCompletionListener(this);
}

void ConnectionPrivate::handleHelloReply()
{
    ConnectionStateChanger stateChanger(this);

    if (!m_helloReceiver->m_helloReply.hasNonErrorReply()) {
        delete m_helloReceiver;
        m_helloReceiver = nullptr;
        stateChanger.setNewState(Unconnected);
        // TODO set an error, provide access to it, also set it on messages when trying to send / receive them
        return;
    }
    Message msg = m_helloReceiver->m_helloReply.takeReply();
    const Arguments &argList = msg.arguments();
    delete m_helloReceiver;
    m_helloReceiver = nullptr;

    Arguments::Reader reader(argList);
    assert(reader.state() == Arguments::String);
    cstring busName = reader.readString();
    assert(reader.state() == Arguments::Finished);
    m_uniqueName = toStdString(busName);

    // tell current secondaries
    UniqueNameReceivedEvent evt;
    evt.uniqueName = m_uniqueName;
    for (auto &it : m_secondaryThreadLinks) {
        CommutexLocker otherLocker(&it.second);
        if (otherLocker.hasLock()) {
            EventDispatcherPrivate::get(it.first->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(new UniqueNameReceivedEvent(evt)));
        }
    }

    stateChanger.setNewState(Connected);
}

void ConnectionPrivate::notifyStateChange(Connection::State oldUserState, Connection::State newUserState)
{
    if (m_connectionStateListener) {
        m_connectionStateListener->handleConnectionChanged(m_connection, oldUserState, newUserState);
    }
}

void ConnectionPrivate::handleClientConnected()
{
    m_transport = m_clientConnectedHandler->m_server->takeNextClient();
    delete m_clientConnectedHandler;
    m_clientConnectedHandler = nullptr;

    assert(m_transport);
    addIoListener(m_transport);
    receiveNextMessage();

    ConnectionStateChanger stateChanger(this, Connected);
}

void Connection::setDefaultReplyTimeout(int msecs)
{
    d->m_defaultTimeout = msecs;
}

int Connection::defaultReplyTimeout() const
{
    return d->m_defaultTimeout;
}

uint32 ConnectionPrivate::takeNextSerial()
{
    uint32 ret;
    do {
        ret = m_sendSerial.fetch_add(1, std::memory_order_relaxed);
    } while (unlikely(ret == 0));
    return ret;
}

Error ConnectionPrivate::prepareSend(Message *msg)
{
    if (msg->serial() == 0) {
        if (!m_mainThreadConnection) {
            msg->setSerial(takeNextSerial());
        } else {
            // we take a serial from the other Connection and then serialize locally in order to keep the CPU
            // expense of serialization local, even though it's more complicated than doing everything in the
            // other thread / Connection.
            CommutexLocker locker(&m_mainThreadLink);
            if (locker.hasLock()) {
                msg->setSerial(m_mainThreadConnection->takeNextSerial());
            } else {
                return Error::LocalDisconnect;
            }
        }
    }

    MessagePrivate *const mpriv = MessagePrivate::get(msg); // this is unchanged by move()ing the owning Message.
    if (!mpriv->serialize()) {
        return mpriv->m_error;
    }
    return Error::NoError;
}

void ConnectionPrivate::sendPreparedMessage(Message msg)
{
    MessagePrivate *const mpriv = MessagePrivate::get(&msg);
    mpriv->setCompletionListener(this);
    m_sendQueue.push_back(std::move(msg));
    if (m_state == ConnectionPrivate::Connected && m_sendQueue.size() == 1) {
        // first in queue, don't wait for some other event to trigger sending
        mpriv->send(m_transport);
    }
}

PendingReply Connection::send(Message m, int timeoutMsecs)
{
    if (timeoutMsecs == DefaultTimeout) {
        timeoutMsecs = d->m_defaultTimeout;
    }

    Error error = d->prepareSend(&m);

    PendingReplyPrivate *pendingPriv = new PendingReplyPrivate(d->m_eventDispatcher, timeoutMsecs);
    pendingPriv->m_connectionOrReply.connection = d;
    pendingPriv->m_receiver = nullptr;
    pendingPriv->m_serial = m.serial();

    // even if we're handing off I/O to a main Connection, keep a record because that simplifies
    // aborting all pending replies when we disconnect from the main Connection, no matter which
    // side initiated the disconnection.
    d->m_pendingReplies.emplace(m.serial(), pendingPriv);

    if (error.isError() || d->m_state == ConnectionPrivate::Unconnected) {
        // Signal the error asynchronously, in order to get the same delayed completion callback as in
        // the non-error case. This should make the behavior more predictable and client code harder to
        // accidentally get wrong. To detect errors immediately, PendingReply::error() can be used.

        // An intentionally locally disconnected connection is not in an error state, but trying to send
        // a message over it is an error.
        pendingPriv->m_error = error.isError() ? error : Error::LocalDisconnect;
        pendingPriv->m_replyTimeout.start(0);
    } else {
        if (!d->m_mainThreadConnection) {
            d->sendPreparedMessage(std::move(m));
        } else {
            CommutexLocker locker(&d->m_mainThreadLink);
            if (locker.hasLock()) {
                std::unique_ptr<SendMessageWithPendingReplyEvent> evt(new SendMessageWithPendingReplyEvent);
                evt->message = std::move(m);
                evt->connection = d;
                EventDispatcherPrivate::get(d->m_mainThreadConnection->m_eventDispatcher)
                    ->queueEvent(std::move(evt));
            } else {
                pendingPriv->m_error = Error::LocalDisconnect;
            }
        }
    }

    return PendingReply(pendingPriv);
}

Error Connection::sendNoReply(Message m)
{
    // ### (when not called from send()) warn if sending a message without the noreply flag set?
    //     doing that is wasteful, but might be common. needs investigation.
    Error error = d->prepareSend(&m);
    if (error.isError() || d->m_state == ConnectionPrivate::Unconnected) {
        return error.isError() ? error : Error::LocalDisconnect;
    }

    // pass ownership to the send queue now because if the IO system decided to send the message without
    // going through an event loop iteration, handleCompletion would be called and expects the message to
    // be in the queue

    if (!d->m_mainThreadConnection) {
        d->sendPreparedMessage(std::move(m));
    } else {
        CommutexLocker locker(&d->m_mainThreadLink);
        if (locker.hasLock()) {
            std::unique_ptr<SendMessageEvent> evt(new SendMessageEvent);
            evt->message = std::move(m);
            EventDispatcherPrivate::get(d->m_mainThreadConnection->m_eventDispatcher)
                ->queueEvent(std::move(evt));
        } else {
            return Error::LocalDisconnect;
        }
    }
    return Error::NoError;
}

size_t Connection::sendQueueLength() const
{
    return d->m_sendQueue.size();
}

void Connection::waitForConnectionEstablished()
{
    if (d->m_state != ConnectionPrivate::Authenticating) {
        return;
    }
    while (d->m_state == ConnectionPrivate::Authenticating) {
        d->m_authClient->handleTransportCanRead();
    }
    if (d->m_state != ConnectionPrivate::AwaitingUniqueName) {
        return;
    }
    // Send the hello message
    assert(!d->m_sendQueue.empty()); // the hello message should be in the queue
    MessagePrivate *helloPriv = MessagePrivate::get(&d->m_sendQueue.front());
    helloPriv->handleTransportCanWrite();

    // Receive the hello reply
    while (d->m_state == ConnectionPrivate::AwaitingUniqueName) {
        MessagePrivate::get(d->m_receivingMessage)->handleTransportCanRead();
    }
}

ConnectAddress Connection::connectAddress() const
{
    return d->m_connectAddress;
}

std::string Connection::uniqueName() const
{
    return d->m_uniqueName;
}

bool Connection::isConnected() const
{
    return d->m_transport && d->m_transport->isOpen();
}

EventDispatcher *Connection::eventDispatcher() const
{
    return d->m_eventDispatcher;
}

IMessageReceiver *Connection::spontaneousMessageReceiver() const
{
    return d->m_client;
}

void Connection::setSpontaneousMessageReceiver(IMessageReceiver *receiver)
{
    d->m_client = receiver;
}

IConnectionStateListener *Connection::connectionStateListener() const
{
    return d->m_connectionStateListener;
}

void Connection::setConnectionStateListener(IConnectionStateListener *listener)
{
    d->m_connectionStateListener = listener;
}

void ConnectionPrivate::handleCompletion(void *task)
{
    switch (m_state) {
    case Authenticating: {
        ConnectionStateChanger stateChanger(this);
        assert(task == m_authClient);
        if (!m_authClient->isAuthenticated()) {
            stateChanger.setNewState(Unconnected);
        }
        delete m_authClient;
        m_authClient = nullptr;
        if (m_state == Unconnected) {
            break;
        }

        stateChanger.setNewState(AwaitingUniqueName);

        // Announce our presence to the bus and have it send some introductory information of its own
        Message hello = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "Hello");
        hello.setSerial(1);
        hello.setExpectsReply(false);
        hello.setDestination(std::string("org.freedesktop.DBus"));
        MessagePrivate *const helloPriv = MessagePrivate::get(&hello);

        m_helloReceiver = new HelloReceiver;
        m_helloReceiver->m_helloReply = m_connection->send(std::move(hello));
        // Small hack: Connection::send() refuses to really start sending if the connection isn't in
        // Connected state. So force the sending here to actually get to Connected state.
        helloPriv->send(m_transport);
        // Also ensure that the hello message is sent before any other messages that may have been
        // already enqueued by an API client
        if (m_sendQueue.size() > 1) {
            hello = std::move(m_sendQueue.back());
            m_sendQueue.pop_back();
            m_sendQueue.push_front(std::move(hello));
        }
        m_helloReceiver->m_helloReply.setReceiver(m_helloReceiver);
        m_helloReceiver->m_parent = this;
        // get ready to receive the first message, the hello reply
        receiveNextMessage();

        break;
    }
    case AwaitingUniqueName: // the code paths for these two states only diverge in the PendingReply handler
    case Connected: {
        assert(!m_authClient);
        if (!m_sendQueue.empty() && task == &m_sendQueue.front()) {
            m_sendQueue.pop_front();
            if (!m_sendQueue.empty()) {
                MessagePrivate::get(&m_sendQueue.front())->send(m_transport);
            }
        } else {
            assert(task == m_receivingMessage);
            Message *const receivedMessage = m_receivingMessage;

            receiveNextMessage();

            if (receivedMessage->type() == Message::InvalidMessage) {
                delete receivedMessage;
            } else if (!maybeDispatchToPendingReply(receivedMessage)) {
                if (m_client) {
                    m_client->handleSpontaneousMessageReceived(Message(std::move(*receivedMessage)),
                                                               m_connection);
                }
                // dispatch to other threads listening to spontaneous messages, if any
                for (auto it = m_secondaryThreadLinks.begin(); it != m_secondaryThreadLinks.end(); ) {
                    SpontaneousMessageReceivedEvent *evt = new SpontaneousMessageReceivedEvent();
                    if (std::next(it) != m_secondaryThreadLinks.end()) {
                        evt->message = *receivedMessage;
                    } else {
                        evt->message = std::move(*receivedMessage);
                    }

                    CommutexLocker otherLocker(&it->second);
                    if (otherLocker.hasLock()) {
                        EventDispatcherPrivate::get(it->first->m_eventDispatcher)
                            ->queueEvent(std::unique_ptr<Event>(evt));
                        ++it;
                    } else {
                        ConnectionPrivate *connection = it->first;
                        it = m_secondaryThreadLinks.erase(it);
                        discardPendingRepliesForSecondaryThread(connection);
                        delete evt;
                    }
                }
                delete receivedMessage;
            }
        }
        break;
    }
    default:
        // ### decide what to do here
        break;
    };
}

bool ConnectionPrivate::maybeDispatchToPendingReply(Message *receivedMessage)
{
    if (receivedMessage->type() != Message::MethodReturnMessage &&
        receivedMessage->type() != Message::ErrorMessage) {
        return false;
    }

    auto it = m_pendingReplies.find(receivedMessage->replySerial());
    if (it == m_pendingReplies.end()) {
        return false;
    }

    if (PendingReplyPrivate *pr = it->second.asPendingReply()) {
        m_pendingReplies.erase(it);
        assert(!pr->m_isFinished);
        pr->handleReceived(receivedMessage);
    } else {
        // forward to other thread's Connection
        ConnectionPrivate *connection = it->second.asConnection();
        m_pendingReplies.erase(it);
        assert(connection);
        PendingReplySuccessEvent *evt = new PendingReplySuccessEvent;
        evt->reply = std::move(*receivedMessage);
        delete receivedMessage;
        EventDispatcherPrivate::get(connection->m_eventDispatcher)->queueEvent(std::unique_ptr<Event>(evt));
    }
    return true;
}

bool ConnectionPrivate::maybeDispatchToPendingReply(uint32 serial, Error error)
{
    assert(error.isError());
    auto it = m_pendingReplies.find(serial);
    if (it == m_pendingReplies.end()) {
        return false;
    }

    if (PendingReplyPrivate *pr = it->second.asPendingReply()) {
        m_pendingReplies.erase(it);
        assert(!pr->m_isFinished);
        pr->handleError(error);
    } else {
        // forward to other thread's Connection
        ConnectionPrivate *connection = it->second.asConnection();
        m_pendingReplies.erase(it);
        assert(connection);
        PendingReplyFailureEvent *evt = new PendingReplyFailureEvent;
        evt->m_serial = serial;
        evt->m_error = error;
        EventDispatcherPrivate::get(connection->m_eventDispatcher)->queueEvent(std::unique_ptr<Event>(evt));
    }
    return true;
}

void ConnectionPrivate::receiveNextMessage()
{
    m_receivingMessage = new Message;
    MessagePrivate *const mpriv = MessagePrivate::get(m_receivingMessage);
    mpriv->setCompletionListener(this);
    mpriv->receive(m_transport);
}

void ConnectionPrivate::unregisterPendingReply(PendingReplyPrivate *p)
{
    if (m_mainThreadConnection) {
        CommutexLocker otherLocker(&m_mainThreadLink);
        if (otherLocker.hasLock()) {
            PendingReplyCancelEvent *evt = new PendingReplyCancelEvent;
            evt->serial = p->m_serial;
            EventDispatcherPrivate::get(m_mainThreadConnection->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(evt));
        }
    }
#ifndef NDEBUG
    auto it = m_pendingReplies.find(p->m_serial);
    assert(it != m_pendingReplies.end());
    if (!m_mainThreadConnection) {
        assert(it->second.asPendingReply());
        assert(it->second.asPendingReply() == p);
    }
#endif
    m_pendingReplies.erase(p->m_serial);
}

void ConnectionPrivate::cancelAllPendingReplies(Error withError)
{
    // No locking because we should have no connections to other threads anymore at this point.
    // No const iteration followed by container clear because that has different semantics - many
    // things can happen in a callback...
    // In case we have pending replies for secondary threads, and we cancel all pending replies,
    // that is because we're shutting down, which we told the secondary thread, and it will deal
    // with bulk cancellation of replies. We just throw away our records about them.
    for (auto it = m_pendingReplies.begin() ; it != m_pendingReplies.end(); ) {
        PendingReplyPrivate *pendingPriv = it->second.asPendingReply();
        it = m_pendingReplies.erase(it);
        if (pendingPriv) { // if from this thread
            pendingPriv->handleError(withError);
        }
    }
    m_sendQueue.clear();
}

void ConnectionPrivate::discardPendingRepliesForSecondaryThread(ConnectionPrivate *connection)
{
    for (auto it = m_pendingReplies.begin() ; it != m_pendingReplies.end(); ) {
        if (it->second.asConnection() == connection) {
            it = m_pendingReplies.erase(it);
            // notification and deletion are handled on the event's source thread
        } else {
            ++it;
        }
    }
}

void ConnectionPrivate::processEvent(Event *evt)
{
    // std::cerr << "ConnectionPrivate::processEvent() with event type " << evt->type << std::endl;

    switch (evt->type) {
    case Event::SendMessage:
        sendPreparedMessage(std::move(static_cast<SendMessageEvent *>(evt)->message));
        break;

    case Event::SendMessageWithPendingReply: {
        SendMessageWithPendingReplyEvent *pre = static_cast<SendMessageWithPendingReplyEvent *>(evt);
        m_pendingReplies.emplace(pre->message.serial(), pre->connection);
        sendPreparedMessage(std::move(pre->message));
        break;
    }
    case Event::SpontaneousMessageReceived:
        if (m_client) {
            SpontaneousMessageReceivedEvent *smre = static_cast<SpontaneousMessageReceivedEvent *>(evt);
            m_client->handleSpontaneousMessageReceived(Message(std::move(smre->message)), m_connection);
        }
        break;

    case Event::PendingReplySuccess:
        maybeDispatchToPendingReply(&static_cast<PendingReplySuccessEvent *>(evt)->reply);
        break;

    case Event::PendingReplyFailure: {
        PendingReplyFailureEvent *prfe = static_cast<PendingReplyFailureEvent *>(evt);
        const auto it = m_pendingReplies.find(prfe->m_serial);
        if (it == m_pendingReplies.end()) {
            // not a disaster, but when it happens in debug mode I want to check it out
            assert(false);
            break;
        }
        PendingReplyPrivate *pendingPriv = it->second.asPendingReply();
        m_pendingReplies.erase(it);
        pendingPriv->handleError(prfe->m_error);
        break;
    }

    case Event::PendingReplyCancel:
        // This comes from a secondary thread, which handles PendingReply notification itself.
        m_pendingReplies.erase(static_cast<PendingReplyCancelEvent *>(evt)->serial);
        break;

    case Event::SecondaryConnectionConnect: {
        SecondaryConnectionConnectEvent *sce = static_cast<SecondaryConnectionConnectEvent *>(evt);

        const auto it = find_if(m_unredeemedCommRefs.begin(), m_unredeemedCommRefs.end(),
                            [sce](const CommutexPeer &item) { return item.id() == sce->id; } );
        assert(it != m_unredeemedCommRefs.end());
        const auto emplaced = m_secondaryThreadLinks.emplace(sce->connection, std::move(*it)).first;
        m_unredeemedCommRefs.erase(it);

        // "welcome package" - it's done (only) as an event to avoid locking order issues
        CommutexLocker locker(&emplaced->second);
        if (locker.hasLock()) {
            UniqueNameReceivedEvent *evt = new UniqueNameReceivedEvent;
            evt->uniqueName = m_uniqueName;
            EventDispatcherPrivate::get(sce->connection->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(evt));
        }

        break;
    }

    case Event::SecondaryConnectionDisconnect: {
        SecondaryConnectionDisconnectEvent *sde = static_cast<SecondaryConnectionDisconnectEvent *>(evt);
        // delete our records to make sure we don't call into it in the future!
        const auto found = m_secondaryThreadLinks.find(sde->connection);
        if (found == m_secondaryThreadLinks.end()) {
            // looks like we've noticed the disappearance of the other thread earlier
            return;
        }
        m_secondaryThreadLinks.erase(found);
        discardPendingRepliesForSecondaryThread(sde->connection);
        break;
    }
    case Event::MainConnectionDisconnect: {
        // since the main thread *sent* us the event, it already knows to drop all our PendingReplies
        m_mainThreadConnection = nullptr;
        MainConnectionDisconnectEvent *mcde = static_cast<MainConnectionDisconnectEvent *>(evt);
        cancelAllPendingReplies(mcde->error);
        break;
    }
    case Event::UniqueNameReceived:
        // We get this when the unique name became available after we were linked up with the main thread
        m_uniqueName = static_cast<UniqueNameReceivedEvent *>(evt)->uniqueName;
        if (m_state == AwaitingUniqueName) {
            ConnectionStateChanger stateChanger(this);
            stateChanger.setNewState(Connected);
        }
        break;
    }
}

Connection::CommRef Connection::createCommRef()
{
    // TODO this is a good time to clean up "dead" CommRefs, where the counterpart was destroyed.
    CommRef ret;
    ret.connection = d;
    std::pair<CommutexPeer, CommutexPeer> link = CommutexPeer::createLink();
    {
        SpinLocker mainLocker(&d->m_lock);
        d->m_unredeemedCommRefs.emplace_back(std::move(link.first));
    }
    ret.commutex = std::move(link.second);
    return ret;
}

uint32 Connection::supportedFileDescriptorsPerMessage() const
{
    return d->m_transport && d->m_transport->supportedPassingUnixFdsCount();
}
