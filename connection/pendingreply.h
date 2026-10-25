// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef PENDINGREPLY_H
#define PENDINGREPLY_H

#include "error.h"

#include <memory>

class IMessageReceiver;
class Message;
class Connection;
class PendingReplyPrivate;

class DFERRY_EXPORT PendingReply
{
public:
    // Constructs a detached instance, which does not have any reply to wait for:
    // isFinished() == true, error() == Error::Detached
    PendingReply();
    ~PendingReply();

    PendingReply(PendingReply &&other);
    PendingReply &operator=(PendingReply &&other);

    PendingReply(PendingReply &other) = delete;
    void operator=(PendingReply &other) = delete;

    bool isNull() const;
    bool isFinished() const; // received a reply or in a state that will not allow receiving a reply
    bool hasNonErrorReply() const; // isFinished() && !isError()

    // Since outgoing messages are only fully validated when trying to send them, Error contains
    // many errors that are typically detected before or while sending and will prevent sending
    // the outgoing message.
    // If a malformed message is sent, the peer might close the connection without notice. That
    // would usually indicate a bug on the sender (this) side - we try to prevent sending malformed
    // messages as far as possible.

    Error error() const;
    bool isError() const; // convenience: error() == Error::None

    void setCookie(void *cookie);
    void *cookie() const;

    void setReceiver(IMessageReceiver *receiver);
    IMessageReceiver *receiver() const;

    const Message *reply() const;
    Message takeReply();

private:
    friend class Connection;
    PendingReply(PendingReplyPrivate *priv); // PendingReplies make no sense to construct "free-standing"
    PendingReplyPrivate *d;
};

#endif // PENDINGREPLY_H
