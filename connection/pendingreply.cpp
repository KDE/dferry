// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "pendingreply.h"
#include "pendingreply_p.h"

#include "connection.h"
#include "connection_p.h"
#include "imessagereceiver.h"
#include "message_p.h"

#include <cassert>
#include <iostream>

#ifndef RUNNING_DOXYGEN
void PendingReplyPrivate::handleReceived(Message *reply)
{
    m_isFinished = true;
    // Connection has already unregistered us because it knows this reply is done
    Connection *const connection = m_connectionOrReply.connection->m_connection;
    m_connectionOrReply.reply = reply;
    m_replyTimeout.stop();
    if (m_receiver) {
        m_receiver->handlePendingReplyFinished(m_owner, connection);
    }
}

void PendingReplyPrivate::handleCompletion(void *task)
{
    assert(task == &m_replyTimeout);
    (void) task;
    assert(!m_isFinished);
    // if a reply comes after the timout, it's too late and the reply is probably served as a spontaneous
    // message by Connection
    if (m_connectionOrReply.connection) {
        m_connectionOrReply.connection->unregisterPendingReply(this);
    }
    handleError(Error::Timeout);
}

void PendingReplyPrivate::handleError(Error error)
{
    // When there is an error before or during sending, we already have an error, and the timeout it set to
    // zero seconds instead of calling the callback right away, in order to provide more consistent behavior
    // to API clients. In that case, the timeout itself is not the error.
    if (!m_error.isError()) {
        assert(error.isError());
        m_error = error;
    }
    m_isFinished = true;
    Connection *const connection = m_connectionOrReply.connection->m_connection;
    m_connectionOrReply.reply = nullptr;
    if (m_receiver) {
        m_receiver->handlePendingReplyFinished(m_owner, connection);
    }
}
#endif // RUNNING_DOXYGEN

/** \class PendingReply
    Waits for a reply to a Message.

    PendingReply notifies an IMessageReceiver when a reply arrived or when an error occured
    at any point in the chain of events that leads to receiving a reply.

    \see Message, Connection::send(), TODO error chaining
*/

/// Constructs an invalid pending reply.
PendingReply::PendingReply()
   : d(nullptr)
{
}

PendingReply::~PendingReply()
{
    if (!d) {
        return;
    }
    if (!d->m_isFinished) {
        if (d->m_connectionOrReply.connection) {
            d->m_connectionOrReply.connection->unregisterPendingReply(d);
        }
    } else {
        if (d->m_connectionOrReply.reply) {
            delete d->m_connectionOrReply.reply;
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

/// Move-constructs a pending reply from \p other.
PendingReply::PendingReply(PendingReply &&other)
   : d(other.d)
{
    other.d = nullptr;
    if (d) {
        d->m_owner = this;
    }
}

/// Move-assigns a pending reply from \p other.
PendingReply &PendingReply::operator=(PendingReply &&other)
{
    if (this == &other) {
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

/// \returns whether this %PendingReply is null.
/// A null %PendingReply is a default-constructed or moved-from one.
bool PendingReply::isNull() const
{
    return !d;
}

/// \returns whether this %PendingReply is in a final state.
/// A final state is either successful completion or definite failure.
bool PendingReply::isFinished() const
{
    return !d || d->m_isFinished;
}

/// \returns whether this %PendingReply has received a non-error reply message.
bool PendingReply::hasNonErrorReply() const
{
    return d && d->m_isFinished && !d->m_error.isError();
}

/// \returns the error that occured on this reply, if any. Otherwise, the return value has error
///          code Error::NoError.
Error PendingReply::error() const
{
    if (!d) {
        return Error::DetachedPendingReply;
    }
    return d->m_error;
}

/// \returns wnether the %PendingReply is in an error state. Error states are not recoverable.
// TODO might want to rethink what to do about "null"
bool PendingReply::isError() const
{
    return d && d->m_error.isError();
}

/// Sets a auxiliary data for application purposes ("cookie").
/// It is typically used to identify the PendingReply to application code.
void PendingReply::setCookie(void *cookie)
{
    d->m_cookie = cookie;
}

/// \returns auxiliary data for application purposes ("cookie").
void *PendingReply::cookie() const
{
    return d->m_cookie;
}

/// Sets a receiver for a reply message or error arriving for this %PendingReply.
/// The receiver will also be notified if an error occurred while trying to send the request message.
/// \see IMessageReceiver
void PendingReply::setReceiver(IMessageReceiver *receiver)
{
    if (d) {
        d->m_receiver = receiver;
    } else {
        // if !d, this is a detached (invalid) instance, and that can't be changed.
        std::cerr << "PendingReply::setReceiver() on a detached instance does nothing.\n";
    }
}

/// \returns the receiver for a reply message or error arriving for this %PendingReply.
/// \see setReceiver
IMessageReceiver *PendingReply::receiver() const
{
    return d ? d->m_receiver : nullptr;
}

/// \returns pointer to reply message if any, nullptr otherwise
const Message *PendingReply::reply() const
{
    return d->m_isFinished ? d->m_connectionOrReply.reply : nullptr;
}

/// \returns the received reply message if any, an empty Message otherwise.
/// If there is a reply, it is moved out of this %PendingReply.
// TODO? error chaining link
Message PendingReply::takeReply()
{
    if (d && d->m_isFinished && d->m_connectionOrReply.reply) {
        // success or error already contained in reply, makes no difference here
        Message reply(std::move(*d->m_connectionOrReply.reply));
        delete d->m_connectionOrReply.reply;
        d->m_connectionOrReply.reply = nullptr;
        return reply;
    }

    // Synthesize a reply message with available error information
    Message reply;
    MessagePrivate *md = MessagePrivate::of(&reply);
    if (!d) {
        md->m_error.setCode(Error::DetachedPendingReply);
    } else if (d->m_error.isError()) {
        md->m_error = d->m_error;
    } else if (!d->m_isFinished) {
        md->m_error.setCode(Error::PendingReplyNotFinished);
    } else {
        assert(!d->m_connectionOrReply.reply); // should have covered all other error cases before
        md->m_error.setCode(Error::DetachedPendingReply);
    }
    return reply;
}
