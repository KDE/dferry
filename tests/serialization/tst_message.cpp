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
#include "connectioninfo.h"
#include "error.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "testutil.h"
#include "transceiver.h"

#include <iostream>

using namespace std;

class PrintAndTerminateClient : public IMessageReceiver
{
public:
    void spontaneousMessageReceived(Message msg) override
    {
        cout << msg.prettyPrint();
        m_eventDispatcher->interrupt();
    }
    EventDispatcher *m_eventDispatcher;
};

class PrintAndReplyClient : public IMessageReceiver
{
public:
    void spontaneousMessageReceived(Message msg) override
    {
        cout << msg.prettyPrint();
        m_transceiver->sendNoReply(Message::createErrorReplyTo(msg, "Unable to get out of hammock!"));
        //m_transceiver->eventDispatcher()->interrupt();
    }
    Transceiver *m_transceiver;
};

// used during implementation, is supposed to not crash and be valgrind-clean afterwards
void testBasic()
{
    EventDispatcher dispatcher;

    ConnectionInfo clientConnection(ConnectionInfo::Bus::PeerToPeer);
    clientConnection.setSocketType(ConnectionInfo::SocketType::AbstractUnix);
    clientConnection.setRole(ConnectionInfo::Role::Client);
    clientConnection.setPath("dferry.Test.Message");

    ConnectionInfo serverConnection = clientConnection;
    serverConnection.setRole(ConnectionInfo::Role::Server);

    Transceiver serverTransceiver(&dispatcher, serverConnection);
    cout << "Created server transceiver. " << &serverTransceiver << endl;
    Transceiver clientTransceiver(&dispatcher, clientConnection);
    cout << "Created client transceiver. " << &clientTransceiver << endl;

    PrintAndReplyClient printAndReplyClient;
    printAndReplyClient.m_transceiver = &serverTransceiver;
    serverTransceiver.setSpontaneousMessageReceiver(&printAndReplyClient);

    PrintAndTerminateClient printAndTerminateClient;
    printAndTerminateClient.m_eventDispatcher = &dispatcher;
    clientTransceiver.setSpontaneousMessageReceiver(&printAndTerminateClient);

    Message msg = Message::createCall("/foo", "org.foo.interface", "laze");
    ArgumentList argList;
    ArgumentList::Writer writer(&argList);
    writer.writeString("couch");
    writer.finish();
    msg.setArgumentList(argList);
#if 0 // maybe future API
    Message *msg = new Message::makeCall("/foo", "org.foo.interface", "laze");
    ArgumentList::Writer writer = msg->writeArguments();
    writer.writeString("couch");
    // writer finalizes automatically, at the latest when sending the message
#endif

    clientTransceiver.sendNoReply(move(msg));

    while (dispatcher.poll()) {
    }
}

int main(int argc, char *argv[])
{
    testBasic();
    // TODO testSaveLoad();
    // TODO testDeepCopy();
    std::cout << "\nNote that the hammock error is part of the test.\nPassed!\n";
}
