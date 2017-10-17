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

#ifndef EVENTDISPATCHER_H
#define EVENTDISPATCHER_H

#include "export.h"

class EventDispatcherPrivate;
class ForeignEventLoopIntegrator;

class DFERRY_EXPORT EventDispatcher
{
public:
#ifndef DFERRY_NO_NATIVE_POLL
    EventDispatcher();
#endif
    EventDispatcher(ForeignEventLoopIntegrator *integrator);
    ~EventDispatcher();
    EventDispatcher(EventDispatcher &other) = delete;
    void operator=(EventDispatcher &other) = delete;

    bool poll(int timeout = -1); // returns false if interrupted by interrupt()
    // Asynchronously interrupt the waiting for events, i.e. at the current (if any) or next poll - this is
    // explicitly allowed to be called from any thread (including its own).
    void interrupt();

private:
    friend class EventDispatcherPrivate;
    EventDispatcherPrivate *d;
};

#endif // EVENTDISPATCHER_H
