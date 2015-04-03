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

#include "argumentlist.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "transceiver.h"

#include "../testutil.h"

#include <iostream>
#include <string>

using namespace std;

static void addressMessageToBus(Message *msg)
{
    msg->setType(Message::MethodCallMessage);
    msg->setDestination(string("org.freedesktop.DBus"));
    msg->setInterface(string("org.freedesktop.DBus"));
    msg->setPath("/org/freedesktop/DBus");
}

class ReplyCheck : public IMessageReceiver
{
public:
    EventDispatcher *m_eventDispatcher;
    void pendingReplyFinished(PendingReply *pr) override
    {
        pr->dumpState();
        std::cout << "got it!\n" << pr->reply()->argumentList().prettyPrint();
        TEST(pr->isFinished());
        TEST(!pr->isError());
        m_eventDispatcher->interrupt();
    }
};

static void testBusAddress(bool waitForConnected)
{
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, ConnectionInfo::Bus::Session);

    Message busNameRequest;
    addressMessageToBus(&busNameRequest);
    busNameRequest.setMethod(string("RequestName"));

    ArgumentList argList;
    ArgumentList::Writer writer(&argList);
    writer.writeString("Bana.nana"); // requested name
    writer.writeUint32(4); // TODO proper enum or so: 4 == DBUS_NAME_FLAG_DO_NOT_QUEUE
    writer.finish();
    busNameRequest.setArgumentList(argList);

    if (waitForConnected) {
        // finish creating the connection
        while (trans.uniqueName().empty()) {
            eventDispatcher.poll();
        }
    }

    PendingReply busNameReply = trans.send(move(busNameRequest));
    ReplyCheck replyCheck;
    replyCheck.m_eventDispatcher = &eventDispatcher;
    busNameReply.setReceiver(&replyCheck);

    while (eventDispatcher.poll()) {
    }

}

int main(int argc, char *argv[])
{
    testBusAddress(false);
    testBusAddress(true);
    std::cout << "Passed!\n";
}
