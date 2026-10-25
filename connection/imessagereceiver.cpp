// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "imessagereceiver.h"

#include "message.h"

/** \class IMessageReceiver
    Interface to receive messages on a Connection and / or a PendingReply.

    \see Connection, PendingReply
*/

IMessageReceiver::~IMessageReceiver()
{
}

/** Called when a Connection receives a message.

    This is called for messages of any kind, including signals and replies.

    TODO what if there is both an IMessageReceiver on the Connection and an IMessageReceiver on
         a PendingReply, will both be notified when the reply message for the PendingReply arrives?
    TODO args

    \see Connection::setSpontaneousMessageReceiver()
*/
void IMessageReceiver::handleSpontaneousMessageReceived(Message /* message */, Connection * /* connection */)
{
    // Message is passed by value: it's gone when this method returns!
}

/** Called when a PendingReply finishes.

    This is called when a PendingReply finishes for any reason, including an error sending
    the request message.

    TODO args

    \see PendingReply::setReceiver()
*/
void IMessageReceiver::handlePendingReplyFinished(PendingReply * /* pendingReply */, Connection *)
{
    // if we get here that might be bad! but it also might not be under special circumstances, so
    // don't complain.
}
