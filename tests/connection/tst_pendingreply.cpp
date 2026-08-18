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

#include "argumentswriter.h"
#include "connectaddress.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "connection.h"

#include "../testutil.h"

#include <iostream>
#include <string>

static void addressMessageToBus(Message *msg)
{
    msg->setType(Message::MethodCallMessage);
    msg->setDestination("org.freedesktop.DBus");
    msg->setInterface("org.freedesktop.DBus");
    msg->setPath("/org/freedesktop/DBus");
}

class ReplyCheck : public IMessageReceiver
{
public:
    void handlePendingReplyFinished(PendingReply *pr, Connection *connection) override
    {
        TEST(pr->isFinished());
        TEST(!pr->isError());

        std::cout << "got it!\n" << pr->reply()->arguments().prettyPrint();

        // This is really a different test, it used to reproduce a memory leak under Valgrind
        Message reply = pr->takeReply();

        connection->eventDispatcher()->interrupt();
    }
};

static void testBusAddress(bool waitForConnected)
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    Message msg;
    addressMessageToBus(&msg);
    msg.setMethod("RequestName");

    ArgumentsWriter writer;
    writer.writeString("Bana.nana"); // requested name
    writer.writeUint32(4); // TODO proper enum or so: 4 == DBUS_NAME_FLAG_DO_NOT_QUEUE
    msg.setArguments(writer.finish());

    if (waitForConnected) {
        // finish creating the connection
        while (conn.uniqueName().empty()) {
            eventDispatcher.poll();
        }
    }

    PendingReply busNameReply = conn.send(std::move(msg));
    ReplyCheck replyCheck;
    busNameReply.setReceiver(&replyCheck);

    while (eventDispatcher.poll()) {
    }
}

class TimeoutCheck : public IMessageReceiver
{
public:
    void handlePendingReplyFinished(PendingReply *reply, Connection *connection) override
    {
        TEST(reply->isFinished());
        TEST(!reply->hasNonErrorReply());
        TEST(reply->error().code() == Error::Timeout);
        std::cout << "We HAVE timed out.\n";
        connection->eventDispatcher()->interrupt();
    }
};

static void testTimeout()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    // finish creating the connection; we need to know our own name so we can send the message to
    // ourself so we can make sure that there will be no reply :)
    while (conn.uniqueName().empty()) {
        eventDispatcher.poll();
    }

    Message msg = Message::createCall("/some/dummy/path", "org.no_interface", "non_existent_method");
    msg.setDestination(conn.uniqueName());

    PendingReply neverGonnaGetReply = conn.send(std::move(msg), 200);

    Message tooEarlyReply = neverGonnaGetReply.takeReply();
    TEST(tooEarlyReply.error().code() == Error::PendingReplyNotFinished);

    TimeoutCheck timeoutCheck;
    neverGonnaGetReply.setReceiver(&timeoutCheck);

    while (eventDispatcher.poll()) {
    }
}

static void testNotConnected()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress());

    Message msg = Message::createCall("/some/dummy/path", "org.no_interface", "non_existent_method");
    msg.setDestination(":1.23456789");

    PendingReply reply = conn.send(std::move(msg), 2000 /*won't actually wait that long*/);
    TEST(!reply.isFinished());
    Message replyMsg = reply.takeReply();
    TEST(replyMsg.error().code() == Error::LocalDisconnect);

    // For compatibility with the non-error case, the transition to Finished is only made at the next
    // event loop iteration.
    eventDispatcher.poll();
    TEST(reply.isFinished());
}

static void testErrorInRequest()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress());

    Message msg = Message::createCall("invalid_path", "org.no_interface", "non_existent_method");
    msg.setDestination(":1.23456789");

    PendingReply reply = conn.send(std::move(msg), 20);
    TEST(!reply.isFinished());

    Message replyMsg = reply.takeReply();
    TEST(replyMsg.error().code() == Error::MessagePath);

    // For compatibility with the non-error case, the transition to Tinished is only made at the next
    // event loop iteration.
    eventDispatcher.poll();
    TEST(reply.isFinished());
}

int main(int, char *[])
{
    testBusAddress(false);
    testBusAddress(true);
    testTimeout();
    testNotConnected();
    testErrorInRequest();
    std::cout << "Passed!\n";
}
