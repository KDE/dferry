/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "stringtools.h"
#include "connection.h"

#include "../testutil.h"

#include <atomic>
#include <iostream>
#include <thread>

static const char *echoPath = "/echo";
// make the name "fairly unique" because the interface name is our only protection against replying
// to the wrong message
static const char *echoInterface = "org.example_fb39a8dbd0aa66d2.echo";
static const char *echoMethod = "echo";

//////////////// Multi-thread ping-pong test ////////////////

static const char *pingPayload = "-> J. Random PING";
static const char *pongPayload = "<- J. Random Pong";

class PongSender : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message ping, Connection *connection) override
    {
        if (ping.interface() != echoInterface) {
            // This is not the ping... it is probably still something from connection setup.
            // We can possibly receive many things here that we were not expecting.
            return;
        }
        {
            Arguments args = ping.arguments();
            Arguments::Reader reader(args);
            cstring payload = reader.readString();
            TEST(!reader.error().isError());
            TEST(reader.isFinished());
            std::cout << "we have ping with payload: " << payload.ptr << std::endl;
        }

        {
            Message pong = Message::createReplyTo(ping);
            Arguments::Writer writer;
            writer.writeString(pongPayload);
            pong.setArguments(writer.finish());

            std::cout << "\n\nSending pong!\n\n";
            Error replyError = connection->sendNoReply(std::move(pong));
            TEST(!replyError.isError());

            connection->eventDispatcher()->interrupt();
        }
    }
};

static void pongThreadRun(Connection::CommRef mainConnectionRef, std::atomic<bool> *pongThreadReady)
{
    std::cout << " Pong thread starting!\n";
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, std::move(mainConnectionRef));

    PongSender pongSender;
    conn.setSpontaneousMessageReceiver(&pongSender);

    while (eventDispatcher.poll()) {
        std::cout << " Pong thread waking up!\n";
        if (conn.uniqueName().length()) {
            pongThreadReady->store(true);
            // HACK: we do this only to wake up the main thread's event loop
            std::cout << "\n\nSending WAKEUP package!!\n\n";
            Message wakey = Message::createCall(echoPath, "org.notexample.foo", echoMethod);
            wakey.setDestination(conn.uniqueName());
            conn.sendNoReply(std::move(wakey));
        } else {
            std::cout << " Pong thread: NO NAME YET!\n";
        }
        // receive ping message
        // send pong message
    }
    std::cout << " Pong thread almost finished!\n";
}

class PongReceiver : public IMessageReceiver
{
public:
    void handlePendingReplyFinished(PendingReply *pongReply, Connection *) override
    {
        TEST(!pongReply->error().isError());
        Message pong = pongReply->takeReply();

        Arguments args = pong.arguments();
        Arguments::Reader reader(args);
        std::string strPayload = toStdString(reader.readString());
        TEST(!reader.error().isError());
        TEST(reader.isFinished());
        TEST(strPayload == pongPayload);
    }
};

static void testPingPong()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    std::atomic<bool> pongThreadReady(false);
    std::thread pongThread(pongThreadRun, conn.createCommRef(), &pongThreadReady);

    // finish creating the connection
    while (conn.uniqueName().empty()) {
        std::cout << ".";
        eventDispatcher.poll();
    }

    std::cout << "we have connection! " << conn.uniqueName() << "\n";

    // send ping message to other thread
    Message ping = Message::createCall(echoPath, echoInterface, echoMethod);
    Arguments::Writer writer;
    writer.writeString(pingPayload);
    ping.setArguments(writer.finish());
    ping.setDestination(conn.uniqueName());

    PongReceiver pongReceiver;
    PendingReply pongReply;

    bool sentPing = false;
    while (!sentPing || !pongReply.isFinished()) {
        eventDispatcher.poll();
        if (pongThreadReady.load() && !sentPing) {
            std::cout << "\n\nSending ping!!\n\n";
            pongReply = conn.send(std::move(ping));
            pongReply.setReceiver(&pongReceiver);
            sentPing = true;
        }
    }
    TEST(pongReply.hasNonErrorReply());

    std::cout << "we have pong!\n";

    pongThread.join();
}

//////////////// Multi-threaded timeout test ////////////////

class TimeoutReceiver : public IMessageReceiver
{
public:
    void handlePendingReplyFinished(PendingReply *reply, Connection *) override
    {
        TEST(reply->isFinished());
        TEST(!reply->hasNonErrorReply());
        TEST(reply->error().code() == Error::Timeout);
        std::cout << "We HAVE timed out.\n";
    }
};

static void timeoutThreadRun(Connection::CommRef mainConnectionRef, std::atomic<bool> *done)
{
    // TODO v turn this into proper documentation in Connection
    // Open a Connection "slaved" to the other Connection - it runs its own event loop in this thread
    // and has message I/O handled by the Connection in the "master" thread through message passing.
    // The main purpose of that is to use just one DBus connection per application( module), which is often
    // more convenient for client programmers and brings some limited ordering guarantees.
    std::cout << " Other thread starting!\n";
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, std::move(mainConnectionRef));
    while (!conn.uniqueName().length()) {
        eventDispatcher.poll();
    }

    Message notRepliedTo = Message::createCall(echoPath, echoInterface, echoMethod);
    notRepliedTo.setDestination(conn.uniqueName());

    PendingReply deadReply = conn.send(std::move(notRepliedTo), 50);
    TimeoutReceiver timeoutReceiver;
    deadReply.setReceiver(&timeoutReceiver);

    while (!deadReply.isFinished()) {
        eventDispatcher.poll();
    }
    *done = true;
}

static void testThreadedTimeout()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    std::atomic<bool> done(false);
    std::thread timeoutThread(timeoutThreadRun, conn.createCommRef(), &done);

    while (!done) {
        eventDispatcher.poll();
    }

    timeoutThread.join();
}


// more things to test:
// - (do we want to do this, and if so here??) blocking on a reply through other thread's connection
// - ping-pong with several messages queued - every message should arrive exactly once and messages
//   should arrive in sending order (can use serials for that as simplificitaion)

int main(int, char *[])
{
    testPingPong();
    testThreadedTimeout();
    std::cout << "Passed!\n";
}
