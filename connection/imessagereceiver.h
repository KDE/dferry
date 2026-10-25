// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IMESSAGERECEIVER_H
#define IMESSAGERECEIVER_H

#include "export.h"

class Connection;
class Message;
class PendingReply;

class DFERRY_EXPORT IMessageReceiver
{
public:
    virtual ~IMessageReceiver();
    // This hands over ownership of the Message. The default implementation is empty, so the Message
    // is destroyed upon going out of scope there.
    virtual void handleSpontaneousMessageReceived(Message message, Connection *connection);
    // This assumes that client code already owns the PendingReply; if the PendingReply was destroyed, the
    // reply would be considered a spontaneous message. The received message is owned by the PendingReply.
    // The default implementation does nothing since somebody must still have the PendingReply, so the
    // Message is still reachable. That's a somewhat strange but valid situation.
    virtual void handlePendingReplyFinished(PendingReply *pendingReply, Connection *connection);
};

#endif // IMESSAGERECEIVER_H
