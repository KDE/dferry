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
    EventDispatcher *m_eventDispatcher;
    void handlePendingReplyFinished(PendingReply *pr, Connection *) override
    {
        pr->dumpState();
        std::cout << "got it!\n" << pr->reply()->arguments().prettyPrint();
        TEST(pr->isFinished());
        TEST(!pr->isError());

        // This is really a different test, it used to reproduce a memory leak under Valgrind
        Message reply = pr->takeReply();

        m_eventDispatcher->interrupt();
    }
};

static void testBusAddress(bool waitForConnected)
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    Message msg;
    addressMessageToBus(&msg);
    msg.setMethod("RequestName");

    Arguments::Writer writer;
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
    replyCheck.m_eventDispatcher = &eventDispatcher;
    busNameReply.setReceiver(&replyCheck);

    while (eventDispatcher.poll()) {
    }
}

class TimeoutCheck : public IMessageReceiver
{
public:
    EventDispatcher *m_eventDispatcher;
    void handlePendingReplyFinished(PendingReply *reply, Connection *) override
    {
        TEST(reply->isFinished());
        TEST(!reply->hasNonErrorReply());
        TEST(reply->error().code() == Error::Timeout);
        std::cout << "We HAVE timed out.\n";
        m_eventDispatcher->interrupt();
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
    TimeoutCheck timeoutCheck;
    timeoutCheck.m_eventDispatcher = &eventDispatcher;
    neverGonnaGetReply.setReceiver(&timeoutCheck);

    while (eventDispatcher.poll()) {
    }
}

int main(int, char *[])
{
    testBusAddress(false);
    testBusAddress(true);
    testTimeout();
    // TODO testBadCall
    std::cout << "Passed!\n";
}
