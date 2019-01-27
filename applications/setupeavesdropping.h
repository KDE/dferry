/*
   Copyright (C) 2019 Andreas Hartmetz <ahartmetz@gmail.com>

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

static Message createEavesdropMessage(const char *messageType)
{
    Message ret = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    ret.setDestination("org.freedesktop.DBus");
    Arguments::Writer writer;
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    ret.setArguments(writer.finish());
    return ret;
}

enum SetupEavesdroppingResult
{
    OldStyleEavesdropping = 0,
    NewStyleEavesdropping,
    FailedEavesdropping
};

static SetupEavesdroppingResult setupEavesdropping(Connection *connection)
{
    {
        // new way to request eavesdropping / monitoring, not yet universally available
        Message msg = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus.Monitoring",
                                          "BecomeMonitor");
        msg.setDestination("org.freedesktop.DBus");
        Arguments::Writer writer;
        writer.beginArray(Arguments::Writer::WriteTypesOfEmptyArray);
        writer.writeString(cstring());
        writer.endArray();
        writer.writeUint32(0);
        msg.setArguments(writer.finish());
        PendingReply pendingReply = connection->send(std::move(msg));
        while (!pendingReply.isFinished()) {
            connection->eventDispatcher()->poll();
        }
        if (!pendingReply.isError()) {
            return NewStyleEavesdropping;
        }
    }

    {
        // old way to request eavesdropping / monitoring, now disabled in some distributions
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };

        std::vector<PendingReply> pendingReplies;
        pendingReplies.reserve(messageTypeCount);
        for (int i = 0; i < messageTypeCount; i++) {
            pendingReplies.emplace_back(connection->send(createEavesdropMessage(messageType[i])));
        }

        bool done = false;
        while (!done) {
            connection->eventDispatcher()->poll();
            done = true;
            for (const PendingReply &PR : pendingReplies) {
                done = done && PR.isFinished();
                if (PR.isError()) {
                    // ### this error detection doesn't seem to work - on my test system with Kubuntu 19.10,
                    // eavesdropping failed silently(!)
                    return FailedEavesdropping;
                }
            }
        }
    }
    return OldStyleEavesdropping;
}
