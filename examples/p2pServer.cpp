// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "argumentswriter.h"
#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher.h"
#include "error.h"
#include "imessagereceiver.h"
#include "message.h"

#include <iostream>

class MyReceiver : public IMessageReceiver
{
    void handleSpontaneousMessageReceived(Message message, Connection *connection) override
    {
        std::cout << "Message received: " << message.method() << ", sending reply\n";

        Message reply = Message::createReplyTo(message);
        ArgumentsWriter writer;
        writer.writeString("pong");
        reply.setArguments(writer.finish());

        connection->sendNoReply(reply); // we don't want a reply to the reply!

        connection->eventDispatcher()->interrupt(); // exit run()
    }
};

int main(int, char *[])
{
    EventDispatcher dispatcher;

    ConnectAddress addr;
#ifdef __linux__
    addr.setType(ConnectAddress::Type::AbstractUnixPath);
    addr.setPath("/dferry/p2pTest");
#else
    addr.setType(ConnectAddress::Type::Tcp);
    addr.setPort(32064); // and IP address is 127.0.0.1 by default, should work fine
#endif
    addr.setRole(ConnectAddress::Role::PeerServer);
    Connection conn(&dispatcher, addr);

    MyReceiver receiver;
    conn.setSpontaneousMessageReceiver(&receiver);

    dispatcher.run();
    return 0;
}
