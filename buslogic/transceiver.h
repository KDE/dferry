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

#include "commutex.h"
#include "types.h"

#include <string>

class ConnectAddress;
class Error;
class EventDispatcher;
class IMessageReceiver;
class Message;
class PendingReply;
class TransceiverPrivate;

class DFERRY_EXPORT Transceiver
{
public:
    enum ThreadAffinity
    {
        MainConnection = 0,
        ThreadLocalConnection
    };

    // Reference for passing to another thread; it guarantees that the target Transceiver
    // either exists or not, but is not currently being destroyed. Yes, the data is all private.
    class CommRef
    {
        friend class Transceiver;
        TransceiverPrivate *transceiver;
        CommutexPeer commutex;
    };

    // for connecting to the session or system bus
    Transceiver(EventDispatcher *dispatcher, const ConnectAddress &connectAddress);
    // for reusing the connection of a Transceiver in another thread
    Transceiver(EventDispatcher *dispatcher, CommRef otherTransceiver);

    ~Transceiver();
    Transceiver(Transceiver &other) = delete;
    Transceiver &operator=(Transceiver &other) = delete;

    CommRef createCommRef();

    void setDefaultReplyTimeout(int msecs);
    int defaultReplyTimeout() const;
    enum TimeoutSpecialValues {
        DefaultTimeout = -1,
        NoTimeout = -2
    };
    // if a message expects no reply, that is not absolutely binding; this method allows to send a message that
    // does not expect (request) a reply, but we get it if it comes - not terribly useful in most cases
    // NOTE: this takes ownership of the message! The message will be deleted after sending in some future
    //       event loop iteration, so it is guaranteed to stay valid before the next event loop iteration.
    PendingReply send(Message m, int timeoutMsecs = DefaultTimeout);
    // Mostly same as above.
    // This one ignores the reply, if any. Reports any locally detectable errors in the return value.
    Error sendNoReply(Message m);

    ConnectAddress connectAddress() const;
    std::string uniqueName() const;
    bool isConnected() const;

    EventDispatcher *eventDispatcher() const;

    // TODO matching patterns for subscription; note that a signal requires path, interface and
    //      "method" (signal name) of sender
    void subscribeToSignal();

    IMessageReceiver *spontaneousMessageReceiver() const;
    void setSpontaneousMessageReceiver(IMessageReceiver *receiver);

private:
    friend class TransceiverPrivate;
    TransceiverPrivate *d;
};

#endif // TRANSCEIVER_H
