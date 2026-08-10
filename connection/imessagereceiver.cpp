/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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
