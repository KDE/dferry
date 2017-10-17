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

#ifndef TIMER_H
#define TIMER_H

#include "types.h"

class EventDispatcher;
class EventDispatcherPrivate;
class ICompletionListener;

class DFERRY_EXPORT Timer
{
public:
    Timer(EventDispatcher *dispatcher);
    ~Timer();
    // can't allow copying because Timers are remembered by pointer in EventDispatcher
    // (and since this isn't a value class, copying makes little sense)
    Timer(const Timer &) = delete;
    void operator=(const Timer &) = delete;

    void start(int msec); // convenience: setInterval(msec) and setRunning(true)
    void stop(); // convenience: setRunning(false)
    void setRunning(bool);
    bool isRunning() const;

    void setInterval(int msec);
    int interval() const;

    void setRepeating(bool);
    bool isRepeating() const;

    int remainingTime() const;

    void setCompletionListener(ICompletionListener *client);
    ICompletionListener *completionClient() const;

    EventDispatcher *eventDispatcher() const;

private:
    friend class EventDispatcherPrivate;
    void trigger();
    EventDispatcher *m_eventDispatcher; // TODO make a per-thread event dispatcher implicit?
    ICompletionListener *m_completionListener;
    bool *m_reentrancyGuard;
    int m_interval;
    bool m_isRunning : 1;
    bool m_isRepeating : 1;
    uint32 m_reserved : sizeof(uint32) - 2;
    uint64 m_tag;
};

#endif // TIMER_H
