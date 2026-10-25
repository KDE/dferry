// SPDX-FileCopyrightText: 2019 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

static Message createEavesdropMessage(const char *messageType)
{
    Message ret = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    ret.setDestination("org.freedesktop.DBus");
    ArgumentsWriter writer;
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
        ArgumentsWriter writer;
        writer.beginArray(ArgumentsWriter::WriteTypesOfEmptyArray);
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
