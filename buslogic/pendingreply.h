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

#ifndef PENDINGREPLY_H
#define PENDINGREPLY_H

#include "types.h"

#include <memory>

class IMessageReceiver;
class Message;
class Transceiver;
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

    bool isFinished() const; // received a reply or in a state that will not allow receiving a reply
    bool hasNonErrorReply() const; // isFinished() && !isError()

    // Since outgoing messages are only fully validated when trying to send them, Error contains
    // many errors that are typically detected before or while sending and will prevent sending
    // the outgoing message.
    // If a malformed message is sent, the peer might close the connection without notice. That
    // would usually indicate a bug on the sender (this) side - we try to prevent sending malformed
    // messages as far as possible.
    enum class Error : uint32 {
        None = 0,
        Detached,
        Timeout,
        Connection,
        MalformedMessage,
        MalformedReply, // Since the reply isn't fully pre-validated for performance reasons,
                        // absence of this error is no guarantee of well-formedness.
        InvalidReceiver,
        NoSuchReceiver,
        InvalidPath,
        NoSuchPath,
        InvalidInterface,
        NoSuchInterface,
        InvalidMethod,
        NoSuchMethod,
        ArgumentTypeMismatch,
        InvalidProperty,
        NoSuchProperty,
        AccessDenied, // for now(?) only properties: writing to read-only / reading from write-only
        Unknown // new ones may be added, so better check for >= UnknownError
    };
    Error error() const;
    bool isError() const; // convenience: error() == Error::None

    void setCookie(void *cookie);
    void *cookie() const;

    void setReceiver(IMessageReceiver *receiver);
    IMessageReceiver *receiver() const;

    const Message *reply() const;
    Message takeReply();

    void dumpState(); // H4X

private:
    friend class Transceiver;
    PendingReply(PendingReplyPrivate *priv); // PendingReplies make no sense to construct "free-standing"
    PendingReplyPrivate *d;
};

#endif // PENDINGREPLY_H
