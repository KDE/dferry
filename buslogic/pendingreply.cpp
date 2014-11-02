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

#include "pendingreply.h"
#include "pendingreply_p.h"

#include "transceiver.h"
#include "transceiver_p.h"

#include <cassert>
#include <iostream>

PendingReply::PendingReply()
   : d(0)
{
}

PendingReply::~PendingReply()
{
    if (!d)  {
        return;
    }
    if (!d->m_isFinished) {
        if (d->m_transceiverOrReply.transceiver) {
            d->m_transceiverOrReply.transceiver->unregisterPendingReply(d);
        }
    } else {
        if (d->m_transceiverOrReply.reply) {
            delete d->m_transceiverOrReply.reply;
        }
    }
    delete d;
    d = nullptr;
}

PendingReply::PendingReply(PendingReplyPrivate *priv)
   : d(priv)
{
    d->m_owner = this;
}

PendingReply::PendingReply(PendingReply &&other)
   : d(other.d)
{
    other.d = nullptr;
    if (d) {
        d->m_owner = this;
    }
}

PendingReply &PendingReply::operator=(PendingReply &&other)
{
    if (this != &other) {
        return *this;
    }
    delete d;
    d = other.d;
    other.d = nullptr;
    // note that in this class, !d is a valid state; otherwise this check wouldn't be necessary because
    // moved-from objects (that also have !d) are not safe for any operation but destruction
    if (d) {
        d->m_owner = this;
    }
    return *this;
}

void PendingReplyPrivate::notifyDone(Message *reply)
{
    m_isFinished = true;
    // Transceiver has already unregistered us because it knows this reply is done
    m_transceiverOrReply.reply = reply;
    m_replyTimeout.stop();
    if (m_client) {
        m_client->notifyCompletion(m_owner);
    }
}

void PendingReply::dumpState()
{
    std::cerr << d << '\n';
    if (d) {
        //std::cerr << d->m_owner << " " << d->m_transceiverOrReply.reply << " " << d->m_serial << " "
        //          << int(d->m_error) << " " << d->m_reply->type() << '\n';
    }
}

bool PendingReply::isFinished() const
{
    return !d || d->m_isFinished;
}

bool PendingReply::hasNonErrorReply() const
{
    return d && d->m_isFinished && d->m_error == Error::None;
}

PendingReply::Error PendingReply::error() const
{
    if (!d) {
        return Error::Detached;
    }
    return d->m_error;
}

bool PendingReply::isError() const
{
    return error() != Error::None;
}

void PendingReply::setCookie(void *cookie)
{
    d->m_cookie = cookie;
}

void *PendingReply::cookie() const
{
    return d->m_cookie;
}

void PendingReply::setCompletionClient(ICompletionClient *client)
{
    if (d) {
        d->m_client = client;
    } else {
        // if !d, this is a detached (invalid) instance, and that can't be changed.
        std::cerr << "PendingReply::setCompletionClient() on a detached instance does nothing.\n";
    }
}

const Message *PendingReply::reply() const
{
    return d->m_isFinished ? d->m_transceiverOrReply.reply : nullptr;
}

std::unique_ptr<Message> PendingReply::takeReply()
{
    Message *reply = nullptr;
    if (d->m_isFinished) {
        reply = d->m_transceiverOrReply.reply;
        d->m_transceiverOrReply.reply = nullptr;
    }
    return std::unique_ptr<Message>(reply);
}

ICompletionClient *PendingReply::completionClient() const
{
    return d ? d->m_client : nullptr;
}

void PendingReplyPrivate::notifyCompletion(void *task)
{
    assert(task == &m_replyTimeout);
    assert(!m_isFinished);
    // if a reply comes after the timout, it's too late and the reply is probably served as a spontaneous
    // message by Transceiver
    if (m_transceiverOrReply.transceiver) {
        m_transceiverOrReply.transceiver->unregisterPendingReply(this);
    }

    m_error = PendingReply::Error::Timeout;
    m_isFinished = true;
    m_transceiverOrReply.reply = nullptr;
    if (m_client) {
        m_client->notifyCompletion(m_owner);
    }
}
