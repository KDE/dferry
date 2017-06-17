/*
   Copyright (C) 2017 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef FOREIGNEVENTLOOPINTEGRATOR_H
#define FOREIGNEVENTLOOPINTEGRATOR_H

#include "export.h"

class EventDispatcher;
class ForeignEventLoopIntegratorPrivate;
class IEventPoller;

class DFERRY_EXPORT ForeignEventLoopIntegrator
{
public:
    ForeignEventLoopIntegrator();
    virtual ~ForeignEventLoopIntegrator();

    bool exiting() const;

    // Implement these to do what their names say; watched events are assumed to be level-triggered,
    // i.e. a file descriptor that is still ready after reading some part of the incoming data should
    // be considered immediately ready again in the next event loop iteration.
    virtual void watchTimeout(int msecs) = 0; // -1 means disable timeout
    virtual void setWatchRead(int fd, bool doWatch) = 0;
    virtual void setWatchWrite(int fd, bool doWatch) = 0;

    // Call these when the watched event occurs
    void handleTimeout();
    void handleReadyRead(int fd);
    void handleReadyWrite(int fd);

    // Call this in the destructor or other shutdown / reset code of the class that implements the pure
    // virtuals. They will be called as necessary to remove all existing watches (read, write, time).
    void removeAllWatches();

private:
    friend class EventDispatcher;
    friend class ForeignEventLoopIntegratorPrivate;
    IEventPoller *connectToDispatcher(EventDispatcher *dispatcher);
    ForeignEventLoopIntegratorPrivate *d;
};

#endif // FOREIGNEVENTLOOPINTEGRATOR_H
