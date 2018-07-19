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

#ifndef CONNECTION_H
#define CONNECTION_H

#include "commutex.h"
#include "types.h"

#include <string>

class ConnectAddress;
class ConnectionPrivate;
class Error;
class EventDispatcher;
class IMessageReceiver;
class ITransport;
class Message;
class PendingReply;
class Server;

class DFERRY_EXPORT Connection
{
public:
    enum ThreadAffinity
    {
        MainConnection = 0,
        ThreadLocalConnection
    };

    // Reference for passing to another thread; it guarantees that the target Connection
    // either exists or not, but is not currently being destroyed. Yes, the data is all private.
    class CommRef
    {
        friend class Connection;
        ConnectionPrivate *connection;
        CommutexPeer commutex;
    };

    // for connecting to the session or system bus
    Connection(EventDispatcher *dispatcher, const ConnectAddress &connectAddress);
    // for reusing the connection of a Connection in another thread
    Connection(EventDispatcher *dispatcher, CommRef otherConnection);

    Connection(Connection &&other);
    Connection &operator=(Connection &&other);

    ~Connection();
    Connection(Connection &other) = delete;
    Connection &operator=(Connection &other) = delete;

    void close();

    CommRef createCommRef();

    bool supportsPassingFileDescriptors() const;

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

    void waitForConnectionEstablished();
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
    friend class Server;
    Connection(ITransport *transport, const ConnectAddress &address); // called from Server

    friend class ConnectionPrivate;
    ConnectionPrivate *d;
};

#endif // CONNECTION_H
