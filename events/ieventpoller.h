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

#ifndef IEVENTPOLLER_H
#define IEVENTPOLLER_H

#include "eventdispatcher.h"

#include "platform.h"

class IioEventClient;

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

    virtual void addIoEventClient(IioEventClient *ioc) = 0;
    virtual void removeIoEventClient(IioEventClient *ioc) = 0;
    virtual void setReadWriteInterest(IioEventClient *ioc, bool read, bool write) = 0;

protected:
    EventDispatcher *m_dispatcher;
};

#endif // IEVENTPOLLER_H
