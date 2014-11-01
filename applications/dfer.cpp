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
#include "eventdispatcher.h"
#include "itransceiverclient.h"
#include "message.h"
#include "transceiver.h"

#include <iostream>
#include <string>

using namespace std;

static Message *createEavesdropMessage(const char *messageType)
{
    Message *ret = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    ret->setDestination("org.freedesktop.DBus");
    ArgumentList argList;
    ArgumentList::Writer writer = argList.beginWrite();
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    writer.finish();
    ret->setArgumentList(argList);
    return ret;
}

class ReplyPrinter : public ITransceiverClient
{
    // reimplemented from ITransceiverClient
    virtual void messageReceived(Message *m);
};

void ReplyPrinter::messageReceived(Message *m)
{
    cout << '\n' << m->prettyPrint();
    delete m;
}

int main(int argc, char *argv[])
{
    EventDispatcher dispatcher;
    Transceiver transceiver(&dispatcher, ConnectionInfo::Bus::Session);
    ReplyPrinter receiver;
    transceiver.setClient(&receiver);
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
