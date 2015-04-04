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
#include "transceiver.h"

#include <iostream>
#include <string>

using namespace std;

static Message createEavesdropMessage(const char *messageType)
{
    Message ret = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    ret.setDestination("org.freedesktop.DBus");
    ArgumentList::Writer writer;
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    ret.setArgumentList(writer.finish());
    return ret;
}

class ReplyPrinter : public IMessageReceiver
{
    // reimplemented from IMessageReceiver
    void spontaneousMessageReceived(Message m) override;
};

void ReplyPrinter::spontaneousMessageReceived(Message m)
{
    cout << '\n' << m.prettyPrint();
}

int main(int argc, char *argv[])
{
    EventDispatcher dispatcher;
    Transceiver transceiver(&dispatcher, ConnectionInfo::Bus::Session);
    ReplyPrinter receiver;
    transceiver.setSpontaneousMessageReceiver(&receiver);
    {
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            transceiver.sendNoReply(createEavesdropMessage(messageType[i]));
        }
    }

    while (true) {
        dispatcher.poll();
    }

    return 0;
}
