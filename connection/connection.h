// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef CONNECTION_H
#define CONNECTION_H

#include "commutex_p.h"
#include "types.h"

#include <string>

class Connection;
class ConnectAddress;
class ConnectionPrivate;
class Error;
class EventDispatcher;
class IConnectionStateListener;
class IMessageReceiver;
class ITransport;
class Message;
class PendingReply;
class Server;

class DFERRY_EXPORT Connection
{
public:
    /// Connection state
    enum State
    {
        Unconnected = 0, ///< Not connected
        Connecting, ///< Connecting / trying to connect
        Connected, ///< Connected
    };

    /// \cond PRIVATE
    enum ThreadAffinity
    {
        MainConnection = 0,
        ThreadLocalConnection,
    };
    /// \endcond

    // Reference for passing to another thread; it guarantees that the target Connection
    // either exists or not, but is not currently being destroyed. Yes, the data is all private.
    /// \private
    class CommRef
    {
        friend class Connection;
        ConnectionPrivate *connection;
        CommutexPeer commutex;
    };

    // for connecting to the session or system bus
    Connection(EventDispatcher *dispatcher, const ConnectAddress &connectAddress);
    /// \cond PRIVATE
    // for reusing the connection of a Connection in another thread
    Connection(EventDispatcher *dispatcher, CommRef mainConnectionRef);
    /// \endcond

    Connection(Connection &&other);
    Connection &operator=(Connection &&other);

    ~Connection();
    Connection(Connection &other) = delete;
    Connection &operator=(Connection &other) = delete;

    State state() const;

    void close();
    /// \cond PRIVATE
    CommRef createCommRef();
    /// \endcond
    uint32 supportedFileDescriptorsPerMessage() const;

    void setDefaultReplyTimeout(int msecs);
    int defaultReplyTimeout() const;
    /// Special values for timeouts
    enum TimeoutSpecialValues {
        DefaultTimeout = -1, ///< Use the default timeout
        NoTimeout = -2 ///< No timeout; if the operation does not complete but also not definitely fail,
                       ///  there will be no notification.
    };
    PendingReply send(Message m, int timeoutMsecs = DefaultTimeout);
    Error sendNoReply(Message m);

    size_t sendQueueLength() const;

    void waitForConnectionEstablished();
    ConnectAddress connectAddress() const;
    std::string uniqueName() const;

    EventDispatcher *eventDispatcher() const;

    IMessageReceiver *spontaneousMessageReceiver() const;
    void setSpontaneousMessageReceiver(IMessageReceiver *receiver);

    IConnectionStateListener *connectionStateListener() const;
    void setConnectionStateListener(IConnectionStateListener *listener);

private:
    friend class Server;
    // called from Server
    Connection(ITransport *transport, EventDispatcher *eventDispatcher, const ConnectAddress &address);

    friend class ConnectionPrivate;
    ConnectionPrivate *d;
};

#endif // CONNECTION_H
