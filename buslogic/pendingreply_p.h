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

#ifndef PENDINGREPLY_P_H
#define PENDINGREPLY_P_H

#include "error.h"
#include "icompletionclient.h"
#include "message.h"
#include "timer.h"

class PendingReply;
class TransceiverPrivate;

class PendingReplyPrivate : public ICompletionClient
{
public:
    PendingReplyPrivate(EventDispatcher *dispatcher, int timeout)
       : m_replyTimeout(dispatcher),
         m_isFinished(false)
    {
        if (timeout >= 0) {
            m_replyTimeout.setRepeating(false);
            m_replyTimeout.setCompletionClient(this);
            m_replyTimeout.start(timeout);
        }
    }

    // for Transceiver
    void notifyDone(Message *reply);
    // for m_replyTimeout
    void notifyCompletion(void *task) override;

    PendingReply *m_owner;
    union {
        TransceiverPrivate *transceiver;
        Message *reply;
    } m_transceiverOrReply;
    void *m_cookie;
    Timer m_replyTimeout;
    IMessageReceiver *m_receiver;
    Error m_error;
    uint32 m_serial;
    bool m_isFinished : 1;
    uint32 m_reserved : 31;
};

#endif // PENDINGREPLY_P_H
