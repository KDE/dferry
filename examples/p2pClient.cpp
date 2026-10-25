// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher.h"
#include "error.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"

#include <iostream>

class MyReceiver : public IMessageReceiver
{
    void handlePendingReplyFinished(PendingReply *reply, Connection *connection) override
    {
        Message replyMsg = reply->takeReply();
        std::cout << "Reply received:\n" << replyMsg.arguments().prettyPrint();

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
    addr.setRole(ConnectAddress::Role::PeerClient);
    Connection conn(&dispatcher, addr);


    std::cout << "Sending ping message...\n";
    MyReceiver receiver;
    PendingReply pending = conn.send(Message::createCall("/", "ping"));
    pending.setReceiver(&receiver);

    dispatcher.run();
    return 0;
}
