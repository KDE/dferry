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
#include "transceiver_p.h"

#include "arguments.h"
#include "authnegotiator.h"
#include "event.h"
#include "eventdispatcher_p.h"
#include "icompletionclient.h"
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
#include <iostream>

using namespace std;

class HelloReceiver : public IMessageReceiver
{
public:
    void pendingReplyFinished(PendingReply *pr) override
    {
        assert(pr == &m_helloReply);
        (void) pr;
        m_parent->handleHelloReply();
    }

    PendingReply m_helloReply; // keep it here so it conveniently goes away when it's done
    TransceiverPrivate *m_parent;
};

class ClientConnectedHandler : public ICompletionClient
{
public:
    ~ClientConnectedHandler()
    {
        delete m_server;
    }

    void notifyCompletion(void *) override
    {
        m_parent->handleClientConnected();
    }

    IServer *m_server;
    TransceiverPrivate *m_parent;
};

TransceiverPrivate::TransceiverPrivate(EventDispatcher *dispatcher)
   : m_state(Unconnected),
     m_client(nullptr),
     m_receivingMessage(nullptr),
     m_connection(nullptr),
     m_helloReceiver(nullptr),
     m_clientConnectedHandler(nullptr),
     m_eventDispatcher(dispatcher),
     m_authNegotiator(nullptr),
     m_defaultTimeout(25000),
     m_sendSerial(1),
     m_mainThreadTransceiver(nullptr)
{
}

Transceiver::Transceiver(EventDispatcher *dispatcher, const ConnectionInfo &ci)
   : d(new TransceiverPrivate(dispatcher))
{
    d->m_connectionInfo = ci;
    assert(d->m_eventDispatcher);
    EventDispatcherPrivate::get(d->m_eventDispatcher)->m_transceiverToNotify = d;

    if (ci.bus() == ConnectionInfo::Bus::None || ci.socketType() == ConnectionInfo::SocketType::None ||
        ci.role() == ConnectionInfo::Role::None) {
        return;
    }

    if (ci.role() == ConnectionInfo::Role::Server) {
        if (ci.bus() == ConnectionInfo::Bus::PeerToPeer) {
            // this sets up a server that will be destroyed after accepting exactly one connection
            d->m_clientConnectedHandler = new ClientConnectedHandler;
            d->m_clientConnectedHandler->m_server = IServer::create(ci);
            d->m_clientConnectedHandler->m_server->setEventDispatcher(dispatcher);
            d->m_clientConnectedHandler->m_server->setNewConnectionClient(d->m_clientConnectedHandler);
            d->m_clientConnectedHandler->m_parent = d;

            d->m_state = TransceiverPrivate::ServerWaitingForClient;
        } else {
            cerr << "Transceiver constructor: bus server not supported.\n";
            // state stays at Unconnected
        }
    } else {
        d->m_connection = IConnection::create(ci);
        d->m_connection->setEventDispatcher(dispatcher);
        if (ci.bus() == ConnectionInfo::Bus::Session || ci.bus() == ConnectionInfo::Bus::System) {
            d->authAndHello(this);
            d->m_state = TransceiverPrivate::Authenticating;
        } else if (ci.bus() == ConnectionInfo::Bus::PeerToPeer) {
            d->receiveNextMessage();
            d->m_state = TransceiverPrivate::Connected;
        }
    }
}

Transceiver::Transceiver(EventDispatcher *dispatcher, CommRef mainTransceiverRef)
   : d(new TransceiverPrivate(dispatcher))
{
    EventDispatcherPrivate::get(d->m_eventDispatcher)->m_transceiverToNotify = d;

    d->m_mainThreadLink = std::move(mainTransceiverRef.commutex);
    CommutexLocker locker(&d->m_mainThreadLink);
    Commutex *const id = d->m_mainThreadLink.id();
    if (!id) {
        assert(false);
        return; // stay in Unconnected state
    }

    // TODO how do we handle m_state?

    d->m_mainThreadTransceiver = mainTransceiverRef.transceiver;
    TransceiverPrivate *mainD = d->m_mainThreadTransceiver;

    // get the current values - if we got them from e.g. the CommRef they could be outdated
    // and we don't want to wait for more event ping-pong
    SpinLocker mainLocker(&mainD->m_lock);
    d->m_connectionInfo = mainD->m_connectionInfo;

    // register with the main Transceiver
    SecondaryTransceiverConnectEvent *evt = new SecondaryTransceiverConnectEvent();
    evt->transceiver = d;
    evt->id = id;
    EventDispatcherPrivate::get(mainD->m_eventDispatcher)
                                ->queueEvent(std::unique_ptr<Event>(evt));
}

Transceiver::~Transceiver()
{
    d->close();

    delete d->m_connection;
    delete d->m_authNegotiator;
    delete d->m_helloReceiver;
    delete d->m_receivingMessage;

    delete d;
    d = nullptr;
}

void TransceiverPrivate::close()
{
    // Can't be main and secondary at the main time - it could be made to work, but what for?
    assert(m_secondaryThreadLinks.empty() || !m_mainThreadTransceiver);

    if (m_mainThreadTransceiver) {
        CommutexUnlinker unlinker(&m_mainThreadLink);
        if (unlinker.hasLock()) {
            SecondaryTransceiverDisconnectEvent *evt = new SecondaryTransceiverDisconnectEvent();
            evt->transceiver = this;
            EventDispatcherPrivate::get(m_mainThreadTransceiver->m_eventDispatcher)
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
                    MainTransceiverDisconnectEvent *evt = new MainTransceiverDisconnectEvent();
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

    cancelAllPendingReplies();

    EventDispatcherPrivate::get(m_eventDispatcher)->m_transceiverToNotify = nullptr;
}

void TransceiverPrivate::authAndHello(Transceiver *parent)
{
    m_authNegotiator = new AuthNegotiator(m_connection);
    m_authNegotiator->setCompletionClient(this);

    // Announce our presence to the bus and have it send some introductory information of its own
    Message hello;
    hello.setType(Message::MethodCallMessage);
    hello.setExpectsReply(false);
    hello.setDestination(std::string("org.freedesktop.DBus"));
    hello.setInterface(std::string("org.freedesktop.DBus"));
    hello.setPath(std::string("/org/freedesktop/DBus"));
    hello.setMethod(std::string("Hello"));

    m_helloReceiver = new HelloReceiver;
    m_helloReceiver->m_helloReply = parent->send(std::move(hello));
    m_helloReceiver->m_helloReply.setReceiver(m_helloReceiver);
    m_helloReceiver->m_parent = this;
}

void TransceiverPrivate::handleHelloReply()
{
    if (!m_helloReceiver->m_helloReply.hasNonErrorReply()) {
        delete m_helloReceiver;
        m_helloReceiver = nullptr;
        m_state = Unconnected;
        // TODO set an error, provide access to it, also set it on messages when trying to send / receive them
        return;
    }
    Arguments argList = m_helloReceiver->m_helloReply.reply()->arguments();
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

    m_state = Connected;
}

void TransceiverPrivate::handleClientConnected()
{
    m_connection = m_clientConnectedHandler->m_server->takeNextConnection();
    delete m_clientConnectedHandler;
    m_clientConnectedHandler = nullptr;

    assert(m_connection);
    m_connection->setEventDispatcher(m_eventDispatcher);
    receiveNextMessage();

    m_state = Connected;
}

void Transceiver::setDefaultReplyTimeout(int msecs)
{
    d->m_defaultTimeout = msecs;
}

int Transceiver::defaultReplyTimeout() const
{
    return d->m_defaultTimeout;
}

uint32 TransceiverPrivate::takeNextSerial()
{
    SpinLocker locker(&m_lock);
    return m_sendSerial++;
}

Error TransceiverPrivate::prepareSend(Message *msg)
{
    if (!m_mainThreadTransceiver) {
        msg->setSerial(takeNextSerial());
    } else {
        // we take a serial from the other Transceiver and then serialize locally in order to keep the CPU
        // expense of serialization local, even though it's more complicated than doing everything in the
        // other thread / Transceiver.
        CommutexLocker locker(&m_mainThreadLink);
        if (locker.hasLock()) {
            msg->setSerial(m_mainThreadTransceiver->takeNextSerial());
        } else {
            return Error::LocalDisconnect;
        }
    }

    MessagePrivate *const mpriv = MessagePrivate::get(msg); // this is unchanged by move()ing the owning Message.
    if (!mpriv->serialize()) {
        return mpriv->m_error;
    }
    return Error::NoError;
}

void TransceiverPrivate::sendPreparedMessage(Message msg)
{
    MessagePrivate *const mpriv = MessagePrivate::get(&msg);
    mpriv->setCompletionClient(this);
    m_sendQueue.push_back(std::move(msg));
    if (m_state == TransceiverPrivate::Connected && m_sendQueue.size() == 1) {
        // first in queue, don't wait for some other event to trigger sending
        mpriv->send(m_connection);
    }
}

PendingReply Transceiver::send(Message m, int timeoutMsecs)
{
    if (timeoutMsecs == DefaultTimeout) {
        timeoutMsecs = d->m_defaultTimeout;
    }

    Error error = d->prepareSend(&m);

    PendingReplyPrivate *pendingPriv = new PendingReplyPrivate(d->m_eventDispatcher, timeoutMsecs);
    pendingPriv->m_transceiverOrReply.transceiver = d;
    pendingPriv->m_receiver = nullptr;
    pendingPriv->m_serial = m.serial();

    // even if we're handing off I/O to a main Transceiver, keep a record because that simplifies
    // aborting all pending replies when we disconnect from the main Transceiver, no matter which
    // side initiated the disconnection.
    d->m_pendingReplies.emplace(m.serial(), pendingPriv);

    if (error.isError()) {
        // Signal the error asynchronously, in order to get the same delayed completion callback as in
        // the non-error case. This should make the behavior more predictable and client code harder to
        // accidentally get wrong. To detect errors immediately, PendingReply::error() can be used.
        pendingPriv->m_error = error;
        pendingPriv->m_replyTimeout.start(0);
    } else {
        if (!d->m_mainThreadTransceiver) {
            d->sendPreparedMessage(std::move(m));
        } else {
            CommutexLocker locker(&d->m_mainThreadLink);
            if (locker.hasLock()) {
                std::unique_ptr<SendMessageWithPendingReplyEvent> evt(new SendMessageWithPendingReplyEvent);
                evt->message = std::move(m);
                evt->transceiver = d;
                EventDispatcherPrivate::get(d->m_mainThreadTransceiver->m_eventDispatcher)
                    ->queueEvent(std::move(evt));
            } else {
                pendingPriv->m_error = Error::LocalDisconnect;
            }
        }
    }

    return PendingReply(pendingPriv);
}

Error Transceiver::sendNoReply(Message m)
{
    // ### (when not called from send()) warn if sending a message without the noreply flag set?
    //     doing that is wasteful, but might be common. needs investigation.
    Error error = d->prepareSend(&m);
    if (error.isError()) {
        return error;
    }

    // pass ownership to the send queue now because if the IO system decided to send the message without
    // going through an event loop iteration, notifyCompletion would be called and expects the message to
    // be in the queue

    if (!d->m_mainThreadTransceiver) {
        d->sendPreparedMessage(std::move(m));
    } else {
        CommutexLocker locker(&d->m_mainThreadLink);
        if (locker.hasLock()) {
            std::unique_ptr<SendMessageEvent> evt(new SendMessageEvent);
            evt->message = std::move(m);
            EventDispatcherPrivate::get(d->m_mainThreadTransceiver->m_eventDispatcher)
                ->queueEvent(std::move(evt));
        } else {
            return Error::LocalDisconnect;
        }
    }
    return Error::NoError;
}

ConnectionInfo Transceiver::connectionInfo() const
{
    return d->m_connectionInfo;
}

std::string Transceiver::uniqueName() const
{
    return d->m_uniqueName;
}

IConnection *Transceiver::connection() const
{
    return d->m_connection;
}

EventDispatcher *Transceiver::eventDispatcher() const
{
    return d->m_eventDispatcher;
}

IMessageReceiver *Transceiver::spontaneousMessageReceiver() const
{
    return d->m_client;
}

void Transceiver::setSpontaneousMessageReceiver(IMessageReceiver *receiver)
{
    d->m_client = receiver;
}

void TransceiverPrivate::notifyCompletion(void *task)
{
    switch (m_state) {
    case Authenticating: {
        assert(task == m_authNegotiator);
        delete m_authNegotiator;
        m_authNegotiator = nullptr;
        // cout << "Authenticated.\n";
        assert(!m_sendQueue.empty()); // the hello message should be in the queue
        MessagePrivate::get(&m_sendQueue.front())->send(m_connection);
        receiveNextMessage();

        m_state = AwaitingUniqueName;
        break;
    }
    case AwaitingUniqueName: // the code path for this only diverges in the PendingReply callback
    case Connected: {
        assert(!m_authNegotiator);
        if (!m_sendQueue.empty() && task == &m_sendQueue.front()) {
            // cout << "Sent message.\n";
            m_sendQueue.pop_front();
            if (!m_sendQueue.empty()) {
                MessagePrivate::get(&m_sendQueue.front())->send(m_connection);
            }
        } else {
            assert(task == m_receivingMessage);
            Message *const receivedMessage = m_receivingMessage;

            receiveNextMessage();

            if (!maybeDispatchToPendingReply(receivedMessage)) {
                if (m_client) {
                    m_client->spontaneousMessageReceived(Message(move(*receivedMessage)));
                }
                // dispatch to other threads listening to spontaneous messages, if any
                for (auto it = m_secondaryThreadLinks.begin(); it != m_secondaryThreadLinks.end(); ) {
                    SpontaneousMessageReceivedEvent *evt = new SpontaneousMessageReceivedEvent();
                    evt->message = *receivedMessage;

                    CommutexLocker otherLocker(&it->second);
                    if (otherLocker.hasLock()) {
                        EventDispatcherPrivate::get(it->first->m_eventDispatcher)
                            ->queueEvent(std::unique_ptr<Event>(evt));
                        ++it;
                    } else {
                        TransceiverPrivate *transceiver = it->first;
                        it = m_secondaryThreadLinks.erase(it);
                        discardPendingRepliesForSecondaryThread(transceiver);
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

bool TransceiverPrivate::maybeDispatchToPendingReply(Message *receivedMessage)
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
        pr->notifyDone(receivedMessage);
    } else {
        // forward to other thread's Transceiver
        TransceiverPrivate *transceiver = it->second.asTransceiver();
        m_pendingReplies.erase(it);
        assert(transceiver);
        PendingReplySuccessEvent *evt = new PendingReplySuccessEvent;
        evt->reply = std::move(*receivedMessage);
        delete receivedMessage;
        EventDispatcherPrivate::get(transceiver->m_eventDispatcher)->queueEvent(std::unique_ptr<Event>(evt));
    }
    return true;
}

void TransceiverPrivate::receiveNextMessage()
{
    m_receivingMessage = new Message;
    MessagePrivate *const mpriv = MessagePrivate::get(m_receivingMessage);
    mpriv->setCompletionClient(this);
    mpriv->receive(m_connection);
}

void TransceiverPrivate::unregisterPendingReply(PendingReplyPrivate *p)
{
    if (m_mainThreadTransceiver) {
        CommutexLocker otherLocker(&m_mainThreadLink);
        if (otherLocker.hasLock()) {
            PendingReplyCancelEvent *evt = new PendingReplyCancelEvent;
            evt->serial = p->m_serial;
            EventDispatcherPrivate::get(m_mainThreadTransceiver->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(evt));
        }
    }
#ifndef NDEBUG
    auto it = m_pendingReplies.find(p->m_serial);
    assert(it != m_pendingReplies.end());
    if (!m_mainThreadTransceiver) {
        assert(it->second.asPendingReply());
        assert(it->second.asPendingReply() == p);
    }
#endif
    m_pendingReplies.erase(p->m_serial);
}

void TransceiverPrivate::cancelAllPendingReplies()
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
            pendingPriv->doErrorCompletion(Error::LocalDisconnect);
        }
    }
}

void TransceiverPrivate::discardPendingRepliesForSecondaryThread(TransceiverPrivate *transceiver)
{
    for (auto it = m_pendingReplies.begin() ; it != m_pendingReplies.end(); ) {
        if (it->second.asTransceiver() == transceiver) {
            it = m_pendingReplies.erase(it);
            // notification and deletion are handled on the event's source thread
        } else {
            ++it;
        }
    }
}

void TransceiverPrivate::processEvent(Event *evt)
{
    // cerr << "TransceiverPrivate::processEvent() with event type " << evt->type << std::endl;

    switch (evt->type) {
    case Event::SendMessage:
        sendPreparedMessage(std::move(static_cast<SendMessageEvent *>(evt)->message));
        break;

    case Event::SendMessageWithPendingReply: {
        SendMessageWithPendingReplyEvent *pre = static_cast<SendMessageWithPendingReplyEvent *>(evt);
        m_pendingReplies.emplace(pre->message.serial(), pre->transceiver);
        sendPreparedMessage(std::move(pre->message));
        break;
    }
    case Event::SpontaneousMessageReceived:
        if (m_client) {
            SpontaneousMessageReceivedEvent *smre = static_cast<SpontaneousMessageReceivedEvent *>(evt);
            m_client->spontaneousMessageReceived(Message(move(smre->message)));
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
        pendingPriv->doErrorCompletion(prfe->m_error);
        break;
    }

    case Event::PendingReplyCancel:
        // This comes from a secondary thread, which handles PendingReply notification itself.
        m_pendingReplies.erase(static_cast<PendingReplyCancelEvent *>(evt)->serial);
        break;

    case Event::SecondaryTransceiverConnect: {
        SecondaryTransceiverConnectEvent *sce = static_cast<SecondaryTransceiverConnectEvent *>(evt);

        const auto it = find_if(m_unredeemedCommRefs.begin(), m_unredeemedCommRefs.end(),
                            [sce](const CommutexPeer &item) { return item.id() == sce->id; } );
        assert(it != m_unredeemedCommRefs.end());
        const auto emplaced = m_secondaryThreadLinks.emplace(sce->transceiver, std::move(*it)).first;
        m_unredeemedCommRefs.erase(it);

        // "welcome package" - it's done (only) as an event to avoid locking order issues
        CommutexLocker locker(&emplaced->second);
        if (locker.hasLock()) {
            UniqueNameReceivedEvent *evt = new UniqueNameReceivedEvent;
            evt->uniqueName = m_uniqueName;
            EventDispatcherPrivate::get(sce->transceiver->m_eventDispatcher)
                ->queueEvent(std::unique_ptr<Event>(evt));
        }

        break;
    }

    case Event::SecondaryTransceiverDisconnect: {
        SecondaryTransceiverDisconnectEvent *sde = static_cast<SecondaryTransceiverDisconnectEvent *>(evt);
        // delete our records to make sure we don't call into it in the future!
        const auto found = m_secondaryThreadLinks.find(sde->transceiver);
        if (found == m_secondaryThreadLinks.end()) {
            // looks like we've noticed the disappearance of the other thread earlier
            return;
        }
        m_secondaryThreadLinks.erase(found);
        discardPendingRepliesForSecondaryThread(sde->transceiver);
        break;
    }
    case Event::MainTransceiverDisconnect:
        // since the main thread *sent* us the event, it already knows to drop all our PendingReplies
        m_mainThreadTransceiver = nullptr;
        cancelAllPendingReplies();
        break;

    case Event::UniqueNameReceived:
        // We get this when the unique name became available after we were linked up with the main thread
        m_uniqueName = static_cast<UniqueNameReceivedEvent *>(evt)->uniqueName;
        break;

    }
}

Transceiver::CommRef Transceiver::createCommRef()
{
    // TODO this is a good time to clean up "dead" CommRefs, where the counterpart was destroyed.
    CommRef ret;
    ret.transceiver = d;
    pair<CommutexPeer, CommutexPeer> link = CommutexPeer::createLink();
    {
        SpinLocker mainLocker(&d->m_lock);
        d->m_unredeemedCommRefs.emplace_back(move(link.first));
    }
    ret.commutex = move(link.second);
    return ret;
}
