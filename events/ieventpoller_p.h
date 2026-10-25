// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IEVENTPOLLER_P_H
#define IEVENTPOLLER_P_H

#include "eventdispatcher.h"

#include "platform_p.h"
#include "types.h"

class IioEventListener;

class IEventPoller
{
public:
    enum InterruptAction {
        NoInterrupt = 0,
        ProcessAuxEvents,
        Stop
    };

    // if you need to refer to the dispatcher, grab and save the value here - not all implementations
    // need it
    IEventPoller(EventDispatcher *dispatcher);
    virtual ~IEventPoller();

    virtual InterruptAction poll(int timeout = -1) = 0;
    // interrupt the waiting for events (from another thread)
    virtual void interrupt(InterruptAction action) = 0;

    virtual void addFileDescriptor(FileDescriptor fd, uint32 ioRw) = 0;
    virtual void removeFileDescriptor(FileDescriptor fd) = 0;
    virtual void setReadWriteInterest(FileDescriptor fd, uint32 ioRw) = 0;

protected:
    EventDispatcher *m_dispatcher;
};

#endif // IEVENTPOLLER_P_H
