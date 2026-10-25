// SPDX-FileCopyrightText: 2017 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

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

    virtual void watchTimeout(int msecs) = 0; // -1 means disable timeout
    virtual void setWatchRead(int fd, bool doWatch) = 0;
    virtual void setWatchWrite(int fd, bool doWatch) = 0;

    void handleTimeout();
    void handleReadyRead(int fd);
    void handleReadyWrite(int fd);

    void removeAllWatches();

private:
    friend class EventDispatcher;
    friend class ForeignEventLoopIntegratorPrivate;
    IEventPoller *connectToDispatcher(EventDispatcher *dispatcher);
    ForeignEventLoopIntegratorPrivate *d;
};

#endif // FOREIGNEVENTLOOPINTEGRATOR_H
