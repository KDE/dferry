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
#include "error.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "testutil.h"
#include "transceiver.h"

#include <cstring>
#include <iostream>

using namespace std;

static void test_signatureHeader()
{
    Message msg;
    Arguments::Writer writer;
    writer.writeByte(123);
    writer.writeUint64(1);
    msg.setArguments(writer.finish());
    TEST(msg.signature() == "yt");
}

class PrintAndTerminateClient : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message msg) override
    {
        cout << msg.prettyPrint();
        m_eventDispatcher->interrupt();
    }
    EventDispatcher *m_eventDispatcher;
};

class PrintAndReplyClient : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message msg) override
    {
        cout << msg.prettyPrint();
        m_transceiver->sendNoReply(Message::createErrorReplyTo(msg, "Unable to get out of hammock!"));
        //m_transceiver->eventDispatcher()->interrupt();
    }
    Transceiver *m_transceiver;
};

// used during implementation, is supposed to not crash and be valgrind-clean afterwards
void testBasic(const ConnectAddress &clientAddress)
{
    EventDispatcher dispatcher;

    ConnectAddress serverAddress = clientAddress;
    serverAddress.setRole(ConnectAddress::Role::Server);

    Transceiver serverTransceiver(&dispatcher, serverAddress);
    cout << "Created server transceiver. " << &serverTransceiver << endl;
    Transceiver clientTransceiver(&dispatcher, clientAddress);
    cout << "Created client transceiver. " << &clientTransceiver << endl;

    PrintAndReplyClient printAndReplyClient;
    printAndReplyClient.m_transceiver = &serverTransceiver;
    serverTransceiver.setSpontaneousMessageReceiver(&printAndReplyClient);

    PrintAndTerminateClient printAndTerminateClient;
    printAndTerminateClient.m_eventDispatcher = &dispatcher;
    clientTransceiver.setSpontaneousMessageReceiver(&printAndTerminateClient);

    Message msg = Message::createCall("/foo", "org.foo.interface", "laze");
    Arguments::Writer writer;
    writer.writeString("couch");
    msg.setArguments(writer.finish());

    clientTransceiver.sendNoReply(move(msg));

    while (dispatcher.poll()) {
    }
}

void testMessageLength()
{
    static const uint32 bufferSize = Arguments::MaxArrayLength + 1024;
    byte *buffer = static_cast<byte *>(malloc(bufferSize));
    memset(buffer, 0, bufferSize);
    for (int i = 0; i < 2; i++) {
        const bool makeTooLong = i == 1;

        Arguments::Writer writer;
        writer.writePrimitiveArray(Arguments::Byte, chunk(buffer, Arguments::MaxArrayLength));

        // Our minimal Message is going to have the following variable headers (in that order):
        // Array: 4 byte length prefix
        // PathHeader: 4 byte length prefix
        // MethodHeader: 4 byte length prefix
        // SignatureHeader: 1 byte length prefix

        // This is VERY tedious to calculate, so let's just take it as an experimentally determined value
        uint32 left = Arguments::MaxMessageLength - Arguments::MaxArrayLength - 72;
        if (makeTooLong) {
            left += 1;
        }
        writer.writePrimitiveArray(Arguments::Byte, chunk(buffer, left));

        Message msg = Message::createCall("/a", "x");
        msg.setSerial(1);
        msg.setArguments(writer.finish());
        std::vector<byte> saved = msg.save();
        TEST(msg.error().isError() == makeTooLong);
    }
}

int main(int, char *[])
{
    test_signatureHeader();
#ifdef __linux__
    {
        ConnectAddress clientAddress(ConnectAddress::Bus::PeerToPeer);
        clientAddress.setSocketType(ConnectAddress::SocketType::AbstractUnix);
        clientAddress.setRole(ConnectAddress::Role::Client);
        clientAddress.setPath("dferry.Test.Message");
        testBasic(clientAddress);
    }
#endif
    // TODO: SocketType::Unix works on any Unix-compatible OS, but we'll need to construct a path
    {
        ConnectAddress clientAddress(ConnectAddress::Bus::PeerToPeer);
        clientAddress.setSocketType(ConnectAddress::SocketType::Ip);
        clientAddress.setPort(6800);
        clientAddress.setRole(ConnectAddress::Role::Client);
        testBasic(clientAddress);
    }

    testMessageLength();

    // TODO testSaveLoad();
    // TODO testDeepCopy();
    std::cout << "\nNote that the hammock error is part of the test.\nPassed!\n";
}
