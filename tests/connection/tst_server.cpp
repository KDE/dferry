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

#include "arguments.h"
#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher.h"
#include "iconnectionstatelistener.h"
#include "imessagereceiver.h"
#include "inewconnectionlistener.h"
#include "message.h"
#include "pendingreply.h"
#include "server.h"

#include "../testutil.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

/*

Sequence diagram of successful test runs. There are three runs with three connections each, where what
happens in the second connection changes from test run to test run.

Events are assumed to be asynchronous between threads, unless indicated as in the following example:
Accept connection       <-          Connect to server
                                    Talk to rubber duckie
In plain words: The server must accept after the client starts connecting, not earlier.
In this case, the ordering is enforced naturally, in other cases, auxiliary synchronization is needed.
Note that it WELL POSSIBLE that "talk to rubber duckie" has already happened when the connection is
accepted by the server - the only guarantee is that accept happens after connect.


TODO:
- how to test connections?
- review, add further details


Server thread           .           Client thread

###### First connection - always succeeds

Set up server           ->          Create thread
Accept connection       <-          Connect
Receive TestMsg         <-          Send TestMsg
Send TestReply          ->          Receive TestReply


###### Second connection - succeeds (test run 1) or fails due to closing by client (test run 2)
###### or fals due to closing by server (test run 3)

Signal next connection  -> ...

### test run 1 - connection 2 succeeds

Accept                  <-          Connect
Receive TestMsg         <-          Send TestMsg
Send TestReply          ->          Receive TestReply (test checks this)

### test run 2 - connection 2 failed by client

Accept                  <-          Connect
Receive connection      <-          Close
  closed error (check)

### test run 3 - connection 2 failed by server

Accept                  <-          Connect
                                    Send TestMsg
Close                   ->          Receive failed PendingReply (check)


###### Third connection - always succeeds

Accept                  <-          Connect
Receive TestMsg         <-          Send TestMsg
Send TestReply          ->          Receive TestReply (check)

*/


enum TestConstants
{
    BrokenConnectionIndex = 1,
    ConnectionsPerTestRun = 3,

    NoFailTestRun = 0,
    ClientCloseTestRun = 1,
    ServerCloseTestRun = 2,
    TestRunCount = 3,

    ReplyTimeoutMsecs = 25000 // TODO back to 250
};

//////////////////////// client thread (a secondary thread) /////////////////////

class ClientSideHandlers : public IConnectionStateListener, public IMessageReceiver
{
public:
    ~ClientSideHandlers() {}

    // IConnectionStateListener
    void handleConnectionChanged(Connection *, Connection::State, Connection::State newState) override
    {
        if (newState == Connection::Unconnected && m_testRunIndex == ServerCloseTestRun) {
            std::cerr << "Client thread: handling disconnect" << std::endl;
            m_serverClosedConnections++;
        }
    }

    // IMessageReceiver
    void handlePendingReplyFinished(PendingReply *pr, Connection *) override
    {
        std::cerr << "Client thread: received pong" << " " << pr->hasNonErrorReply() << std::endl;
        if (pr->hasNonErrorReply()) {
            m_receivedSuccessReplies++;
        } else {
            m_receivedErrorReplies++;
        }
        m_eventDispatcher->interrupt();
    }

    EventDispatcher *m_eventDispatcher = nullptr;
    int m_testRunIndex = 0;
    int m_serverClosedConnections = 0;
    int m_receivedSuccessReplies = 0;
    int m_receivedErrorReplies = 0;
};


static void clientThreadRun(ConnectAddress address, int testRunIndex)
{
    EventDispatcher eventDispatcher;
    ClientSideHandlers clientHandlers;
    clientHandlers.m_eventDispatcher = &eventDispatcher;
    clientHandlers.m_testRunIndex = testRunIndex;

    // Client-side connections - these call listeners in ClientSideHandlers when closing during
    // destruction. The easiest way to ensure a non-crashing destruction order is to put them here.
    std::vector<Connection> connections;

    for (int i = 0; i < TestConstants::ConnectionsPerTestRun; i++) {
        std::cerr << "Client thread: test run " << testRunIndex << " / connection " << i << std::endl;
        connections.push_back(Connection(&eventDispatcher, address));
        connections.back().setConnectionStateListener(&clientHandlers);

#if 1
        if (i == TestConstants::BrokenConnectionIndex && testRunIndex == ClientCloseTestRun) {
            std::cerr << "Client thread: closing connection" << std::endl;
            connections.back().close();
            std::cerr << "Client thread: closed connection" << std::endl;
            continue;
        }
#endif
        Message ping = Message::createCall("/foo", "org.bar.interface", "serverTest");
        PendingReply pendingReply = connections.back().send(std::move(ping), ReplyTimeoutMsecs);
        std::cerr << "Client thread: sent ping" << std::endl;
#if 0
        if (i == TestConstants::BrokenConnectionIndex && testRunIndex == ClientCloseTestRun) {
            std::cerr << "Client thread: closing connection" << std::endl;
            while (connections.back().sendQueueLength()) {
                eventDispatcher.poll();
            }
            connections.back().close();
            std::cerr << "Client thread: closed connection" << std::endl;
            continue;
        } else
#endif
        pendingReply.setReceiver(&clientHandlers);

        while (eventDispatcher.poll()) {
        }

        if (i == TestConstants::BrokenConnectionIndex && testRunIndex == ServerCloseTestRun) {
            TEST(pendingReply.error().code() == Error::RemoteDisconnect);
        } else {
            TEST(!pendingReply.error().isError());
        }
    }

    if (testRunIndex == NoFailTestRun) {
        TEST(clientHandlers.m_serverClosedConnections == 0);
        TEST(clientHandlers.m_receivedSuccessReplies == 3);
        TEST(clientHandlers.m_receivedErrorReplies == 0);
    } else if (testRunIndex == ClientCloseTestRun) {
        TEST(clientHandlers.m_serverClosedConnections == 0);
        TEST(clientHandlers.m_receivedSuccessReplies == 2);
        TEST(clientHandlers.m_receivedErrorReplies == 0);
    } else {
        TEST(clientHandlers.m_serverClosedConnections == 1);
        TEST(clientHandlers.m_receivedSuccessReplies == 2);
        TEST(clientHandlers.m_receivedErrorReplies == 1);
    }
}

//////////////////////// server thread (the main thread) /////////////////////

class ServerSideHandlers : public INewConnectionListener, public IConnectionStateListener,
                           public IMessageReceiver
{
public:
    // INewConnectionListener
    void handleNewConnection(Server *server) override
    {
        std::unique_ptr<Connection> conn(server->takeNextClient());
        TEST(conn); // for now this is simply not allowed... we could try to check why this
                    // happened, if it ever happens
        conn->setSpontaneousMessageReceiver(this);
        conn->setConnectionStateListener(this);
        const size_t connectionIndex = m_connections.size();
        m_connections.push_back(std::move(*conn));

        if (connectionIndex == TestConstants::BrokenConnectionIndex &&
            m_testRunIndex == ServerCloseTestRun) {
            m_connections.back().close();
            stopListeningToConnection(&m_connections.back(), "we closed");
        }
    }

    // IConnectionStateListener
    void handleConnectionChanged(Connection *conn, Connection::State oldState,
                                 Connection::State newState) override
    {
        const auto it = std::find_if(m_connections.begin(), m_connections.end(),
                                     [conn] (Connection &c) { return conn == &c; });
        const int connIndex = std::distance(m_connections.begin(), it);

        std::cerr << "Server thread: handling state change @ index " << connIndex
                  << " from " << oldState << " to " << newState << std::endl;
        if (newState != Connection::Unconnected) {
            return;
        }

        std::cerr << "Server thread: handling disconnect @ index " << connIndex << std::endl;
        if (connIndex == TestConstants::BrokenConnectionIndex) {
            if (m_testRunIndex == ClientCloseTestRun) {
                std::cerr << "  *** HURZ ***" << std::endl;
                m_clientClosedConnectionAtTheRightPoint++;
                stopListeningToConnection(conn, "disconnected");
            }
        }
    }

    // IMessageReceiver
    void handleSpontaneousMessageReceived(Message message, Connection *conn) override
    {
        std::cerr << "Server thread: received ping" << std::endl;
        conn->sendNoReply(Message::createReplyTo(message));
        stopListeningToConnection(conn, "ping received");
    }

    int m_testRunIndex = 0;
    std::vector<Connection> m_connections;  // server-side connections
    int m_connectionsFullyHandled = 0;
    int m_clientClosedConnectionAtTheRightPoint = 0;

private:
    void stopListeningToConnection(Connection *conn, const char *reason)
    {
        std::cerr << "Server thread: start ignoring connection because " << reason << std::endl;
        conn->setSpontaneousMessageReceiver(nullptr);
        conn->setConnectionStateListener(nullptr);
        m_connectionsFullyHandled++;
    }
};

static void testAcceptMultiple(int testRunIndex)
{
    // Accept multiple connections and run a ping-pong message test on each. If withFail is true,
    // abort one connection from the client side and check that the rest still works.
    EventDispatcher eventDispatcher;

    ConnectAddress addr;
    addr.setRole(ConnectAddress::Role::PeerServer);
#ifdef __unix__
    addr.setType(ConnectAddress::Type::TmpDir);
    addr.setPath("/tmp");
#else
    addr.setType(ConnectAddress::Type::Tcp);
    addr.setPort(36816 /* randomly selected ;) */);
#endif

    Server server(&eventDispatcher, addr);
    ServerSideHandlers serverHandler;
    serverHandler.m_testRunIndex = testRunIndex;
    server.setNewConnectionListener(&serverHandler);

    ConnectAddress clientAddr = server.concreteAddress();
    clientAddr.setRole(ConnectAddress::Role::PeerClient);
    std::thread clientThread(clientThreadRun, clientAddr, testRunIndex);

    while (serverHandler.m_connectionsFullyHandled < ConnectionsPerTestRun ||
           (serverHandler.m_connections.back().state() != Connection::Unconnected &&
            serverHandler.m_connections.back().sendQueueLength())) {
        eventDispatcher.poll();
    }

    clientThread.join();

    TEST(serverHandler.m_connectionsFullyHandled == ConnectionsPerTestRun);
    if (testRunIndex == ClientCloseTestRun) {
        TEST(serverHandler.m_clientClosedConnectionAtTheRightPoint == 1);
    } else {
        TEST(serverHandler.m_clientClosedConnectionAtTheRightPoint == 0);
    }
}


int main(int, char *[])
{
    for (int i = 0; i < TestRunCount; i++) {
        testAcceptMultiple(i);
    }
    std::cout << "Passed!\n";
}
