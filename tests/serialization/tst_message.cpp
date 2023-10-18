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
#include "pendingreply.h"
#include "testutil.h"
#include "connection.h"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

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
    void handleSpontaneousMessageReceived(Message msg, Connection *connection) override
    {
        std::cout << msg.prettyPrint();
        connection->eventDispatcher()->interrupt();
    }
};

class PrintAndReplyClient : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message msg, Connection *connection) override
    {
        std::cout << msg.prettyPrint();
        connection->sendNoReply(Message::createErrorReplyTo(msg, "Unable to get out of hammock!"));
        //connection->eventDispatcher()->interrupt();
    }
};

// used during implementation, is supposed to not crash and be valgrind-clean afterwards
void testBasic(const ConnectAddress &clientAddress)
{
    EventDispatcher dispatcher;

    ConnectAddress serverAddress = clientAddress;
    serverAddress.setRole(ConnectAddress::Role::PeerServer);

    Connection serverConnection(&dispatcher, serverAddress);
    std::cout << "Created server connection. " << &serverConnection << std::endl;
    Connection clientConnection(&dispatcher, clientAddress);
    std::cout << "Created client connection. " << &clientConnection << std::endl;

    PrintAndReplyClient printAndReplyClient;
    serverConnection.setSpontaneousMessageReceiver(&printAndReplyClient);

    PrintAndTerminateClient printAndTerminateClient;
    clientConnection.setSpontaneousMessageReceiver(&printAndTerminateClient);

    Message msg = Message::createCall("/foo", "org.foo.interface", "laze");
    Arguments::Writer writer;
    writer.writeString("couch");
    msg.setArguments(writer.finish());

    clientConnection.sendNoReply(std::move(msg));

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

enum {
    // a small integer could be confused with an index into the fd array (in the implementation),
    // so make it large
    DummyFdOffset = 1000000
};

#ifdef __unix__
static Arguments createArgumentsWithDummyFileDescriptors(uint fdCount)
{
    Arguments::Writer writer;
    for (uint i = 0; i < fdCount; i++) {
        writer.writeUnixFd(DummyFdOffset - i);
    }
    return writer.finish();
}

void testFileDescriptorsInArguments()
{
    // Note: This replaces round-trip tests with file descriptors in tst_arguments.
    // A full roundtrip test must go through Message due to the out-of-band way that file
    // descriptors are stored (which is so because they are also transmitted out-of-band).
    Message msg = Message::createCall("/foo", "org.foo.interface", "doNothing");
    for (uint i = 0; i < 4; i++) {
        msg.setArguments(createArgumentsWithDummyFileDescriptors(i));
        {
            // const ref to arguments
            const Arguments &args = msg.arguments();
            Arguments::Reader reader(args);
            for (uint j = 0; j < i; j++) {
                TEST(reader.readUnixFd() == int(DummyFdOffset - j));
                TEST(reader.isValid());
            }
            TEST(reader.isFinished());
        }
        {
            // copy of arguments
            Arguments args = msg.arguments();
            Arguments::Reader reader(args);
            for (uint j = 0; j < i; j++) {
                TEST(reader.readUnixFd() == int(DummyFdOffset - j));
                TEST(reader.isValid());
            }
            TEST(reader.isFinished());
        }
    }
}

void testTooManyFileDescriptors()
{
    // TODO re-think what is the best place to catch too many file descriptors...
    Arguments::Writer writer;
}

void testFileDescriptorsHeader()
{
    Message msg = Message::createCall("/foo", "org.foo.interface", "doNothing");
    for (uint i = 0; i < 4; i++) {
        msg.setArguments(createArgumentsWithDummyFileDescriptors(i));
        TEST(msg.unixFdCount() == i);
    }
}

enum {
    // for pipe2() file descriptor array
    ReadSide = 0,
    WriteSide = 1,
    // how many file descriptors to send in test
    FdCountToSend = 10
};

class FileDescriptorTestReceiver : public IMessageReceiver
{
public:
    void handleSpontaneousMessageReceived(Message msg, Connection *connection) override
    {
        // we're on the session bus, so we'll receive all kinds of notifications we don't care about here
        if (msg.type() != Message::MethodCallMessage
            || msg.method() != "testFileDescriptorsForDataTransfer") {
            return;
        }

        Arguments::Reader reader(msg.arguments());
        for (uint i = 0; i < FdCountToSend; i++) {
            int fd = reader.readUnixFd();
            uint readBuf = 12345;
            ::read(fd, &readBuf, sizeof(uint));
            ::close(fd);
            TEST(readBuf == i);
        }
        Message reply = Message::createReplyTo(msg);
        connection->sendNoReply(std::move(reply));
    }
};

void testFileDescriptorsForDataTransfer()
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);
    conn.waitForConnectionEstablished();
    TEST(conn.isConnected());

    int pipeFds[2 * FdCountToSend];

    Message msg = Message::createCall("/foo", "org.foo.interface", "testFileDescriptorsForDataTransfer");
    msg.setDestination(conn.uniqueName());

    Arguments::Writer writer;
    for (uint i = 0; i < FdCountToSend; i++) {
        TEST(pipe2(pipeFds + 2 * i, O_NONBLOCK) == 0);
        // write into write side of the pipe... will be read when the message is received back from bus
        ::write(pipeFds[2 * i + WriteSide], &i, sizeof(uint));

        writer.writeUnixFd(pipeFds[2 * i + ReadSide]);
    }

    msg.setArguments(writer.finish());

    PendingReply reply = conn.send(std::move(msg), 500 /* fail quickly */);
    FileDescriptorTestReceiver fdTestReceiver;
    conn.setSpontaneousMessageReceiver(&fdTestReceiver);

    while (!reply.isFinished()) {
        eventDispatcher.poll();
    }

    if (conn.supportedFileDescriptorsPerMessage() >= FdCountToSend) {
        // TODO should not fail with timeout, should fail quickly and *locally* while trying to send!
        TEST(reply.hasNonErrorReply()); // otherwise timeout, the message exchange failed somehow
    } else {
        TEST(!reply.hasNonErrorReply());
        for (uint i = 0; i < FdCountToSend; i++) {
            ::close(pipeFds[2 * i + ReadSide]);
        }
    }

    for (uint i = 0; i < FdCountToSend; i++) {
        ::close(pipeFds[2 * i + WriteSide]);
    }
}
#endif

void testAssignment()
{
    Message msg1 = Message::createCall("/foo", "org.foo.bar", "someMethod");
    msg1.setSender("sender1");
    Message msg2 = Message::createSignal("/bar", "org.xyz.abc", "thingHappened");
    msg2.setReplySerial(1234);

    msg2 = msg1;
    TEST(msg2.type() == Message::MethodCallMessage);
    TEST(msg2.path() == "/foo");
    TEST(msg2.interface() == "org.foo.bar");
    TEST(msg2.method() == "someMethod");
    TEST(msg2.sender() == "sender1");
    TEST(msg2.replySerial() == 0);
}

int main(int, char *[])
{
    test_signatureHeader();
#ifdef __linux__
    {
        ConnectAddress clientAddress;
        clientAddress.setType(ConnectAddress::Type::AbstractUnixPath);
        clientAddress.setRole(ConnectAddress::Role::PeerClient);
        clientAddress.setPath("dferry.Test.Message");
        testBasic(clientAddress);
    }
#endif
    // TODO: SocketType::Unix works on any Unix-compatible OS, but we'll need to construct a path
    {
        ConnectAddress clientAddress;
        clientAddress.setType(ConnectAddress::Type::Tcp);
        clientAddress.setPort(6800);
        clientAddress.setRole(ConnectAddress::Role::PeerClient);
        testBasic(clientAddress);
    }

    testMessageLength();

#ifdef __unix__
    testFileDescriptorsInArguments();
    testTooManyFileDescriptors();
    testFileDescriptorsHeader();
    testFileDescriptorsForDataTransfer();
#endif
    testAssignment();

    // TODO testSaveLoad();
    // TODO testDeepCopy();
    std::cout << "\nNote that the hammock error is part of the test.\nPassed!\n";
}
