/*
   Copyright (C) 2026 Andreas Hartmetz <ahartmetz@gmail.com>

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
#include "error.h"
#include "eventdispatcher.h"
#include "imessagereceiver.h"
#include "message.h"
#include "pendingreply.h"
#include "connection.h"

#include <iostream>

int main(int, char *[])
{
    EventDispatcher eventDispatcher;
    Connection conn(&eventDispatcher, ConnectAddress::StandardBus::Session);

    // Demo: Any error in the whole send-reply cycle of a message will result in the first error being
    // propagated all the way to the end, which allows error handling with very little error handling code.


    // A very early error - when constructing the Arguments
    {
        ArgumentsWriter writer;
        writer.beginStruct();
        // error: empty struct is not allowed
        writer.endStruct();

        Message msg = Message::createCall("/foo/bar/dferry/testing", "fooMethod");
        msg.setDestination(":1.100"); // just needs valid address syntax, we never actually send the message
        msg.setArguments(writer.finish());

        PendingReply reply = conn.send(std::move(msg));
        // The error could be signaled immediately in send(), but for behavior compatibility with the success
        // case and with errors occurring later, the reply is only marked as finished during event dispatch.
        assert(!reply.isFinished());
        // If synchronous handling is really desired, it can be done explicitly by checking the error
        assert(reply.error().isError());
        assert(reply.error().code() == Error::EmptyStruct);

        while (!reply.isFinished()) {
            eventDispatcher.poll();
        }

        assert(!reply.hasNonErrorReply());
        assert(reply.isError());
        std::cerr << "Early error: reply error is " << reply.error().string() << '\n';

        assert(!reply.reply());
        // This Message is a synthesized one because no actual reply was received
        Message replyMsg = reply.takeReply();
        assert(replyMsg.error().isError());
        std::cerr << "Early error: reply message error is " << reply.error().string() << '\n';
    }


    // A fairly late error - connection closed before message could be sent or received
    // (Sending may be synchronous or asynchronous, it doesn't really matter because receiving the
    // reply is asynchronous in any case. It's intentionally implemented asynchronously even in the
    // "immediate error" case for behavior compatibility.)
    {
        ArgumentsWriter writer;
        writer.beginStruct();
        writer.writeUint32(1000);
        writer.endStruct();

        Message msg = Message::createCall("/foo/bar/dferry/testing", "fooMethod");
        msg.setDestination(":1.100"); // just needs valid address syntax, we never actually send the message
        msg.setArguments(writer.finish());

        PendingReply reply = conn.send(std::move(msg));
        // error: connection closed before a reply could be received
        conn.close();

        while (!reply.isFinished()) {
            eventDispatcher.poll();
        }

        assert(!reply.hasNonErrorReply());
        assert(reply.isError());
        std::cerr << "Late error: reply error is " << reply.error().string() << '\n';

        assert(!reply.reply());
        // This Message is a synthesized one because no actual reply was received
        Message replyMsg = reply.takeReply();
        assert(replyMsg.error().isError());
        std::cerr << "Late error: reply message error is " << reply.error().string() << '\n';
    }

    // Note: There is more in tests/connection/tst_errorpropagation.cpp. It's less readable but more
    // detailed. The code in this file is mostly copied and simplified from there.

    return 0;
}
