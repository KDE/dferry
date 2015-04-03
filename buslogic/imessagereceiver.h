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

#ifndef IMESSAGERECEIVER_H
#define IMESSAGERECEIVER_H

#include "export.h"

class Message;
class PendingReply;

class DFERRY_EXPORT IMessageReceiver
{
public:
    virtual ~IMessageReceiver();
    // This hands over ownership of the Message. The default implementation is empty, so the Message
    // is destroyed upon going out of scope there.
    virtual void spontaneousMessageReceived(Message message);
    // This assumes that client code already owns the PendingReply; if the PendingReply was destroyed, the
    // reply would be considered a spontaneous message. The received message is owned by the PendingReply.
    // The default implementation does nothing since somebody must still have the PendingReply, so the
    // Message is still reachable. That's a somewhat strange but valid situation.
    virtual void pendingReplyFinished(PendingReply *pendingReply);
};

#endif // IMESSAGERECEIVER_H
