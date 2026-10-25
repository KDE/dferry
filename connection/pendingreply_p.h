// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef PENDINGREPLY_P_H
#define PENDINGREPLY_P_H

#include "error.h"
#include "icompletionlistener.h"
#include "message.h"
#include "timer.h"

class PendingReply;
class ConnectionPrivate;

class PendingReplyPrivate : public ICompletionListener
{
public:
    PendingReplyPrivate(EventDispatcher *dispatcher, int timeout)
       : m_replyTimeout(dispatcher),
         m_isFinished(false),
         m_reserved(0)
    {
        if (timeout >= 0) {
            m_replyTimeout.setRepeating(false);
            m_replyTimeout.setCompletionListener(this);
            m_replyTimeout.start(timeout);
        }
    }

    // for Connection
    void handleReceived(Message *reply);
    void handleError(Error error);
    // for m_replyTimeout
    void handleCompletion(void *task) override;

    PendingReply *m_owner = nullptr;
    union {
        ConnectionPrivate *connection = nullptr;
        Message *reply;
    } m_connectionOrReply;
    void *m_cookie = nullptr;
    Timer m_replyTimeout;
    IMessageReceiver *m_receiver = nullptr;
    Error m_error;
    uint32 m_serial = 0;
    bool m_isFinished : 1;
    uint32 m_reserved : 31;
};

#endif // PENDINGREPLY_P_H
