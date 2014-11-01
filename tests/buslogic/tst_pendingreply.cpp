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
#include "icompletionclient.h"
#include "message.h"
#include "pendingreply.h"
//#include "platformtime.h"
//#include "timer.h"
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

static void testBusAddress()
{
    EventDispatcher eventDispatcher;
    Transceiver trans(&eventDispatcher, ConnectionInfo::Bus::Session);

    Message *busNameRequest = new Message;
    addressMessageToBus(busNameRequest);
    busNameRequest->setMethod(string("RequestName"));

    ArgumentList argList;
    ArgumentList::Writer writer = argList.beginWrite();
    writer.writeString("Bana.nana"); // requested name
    writer.writeUint32(4); // TODO proper enum or so: 4 == DBUS_NAME_FLAG_DO_NOT_QUEUE
    writer.finish();
    busNameRequest->setArgumentList(argList);

    PendingReply busNameReply = trans.send(busNameRequest);
    CompletionFunc replyCheck([&eventDispatcher] (void *task)
    {
        PendingReply *pr = static_cast<PendingReply *>(task);
        pr->dumpState();
        std::cout << "got it!\n" << pr->reply().argumentList().prettyPrint();
        TEST(pr->isFinished());
        TEST(!pr->isError());
        eventDispatcher.interrupt();
    });
    busNameReply.setCompletionClient(&replyCheck);

    while (eventDispatcher.poll()) {
    }

}

int main(int argc, char *argv[])
{
    testBusAddress();
    std::cout << "Passed!\n";
}
