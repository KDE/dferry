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

#include "timer.h"

#include "eventdispatcher.h"
#include "eventdispatcher_p.h"
#include "icompletionlistener.h"
#include "platformtime.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

Timer::Timer(EventDispatcher *dispatcher)
   : m_eventDispatcher(dispatcher),
     m_completionListener(nullptr),
     m_reentrancyGuard(nullptr),
     m_interval(0),
     m_isRunning(false),
     m_isRepeating(true),
     m_nextDueTime(0),
     m_serial(0)
{
}

Timer::~Timer()
{
    // Rationale for "|| m_reentrancyGuard": While triggered, we must be removed from the event
    // dispatcher's timer map before it may dereference the then dangling pointer to this Timer.
    if (m_isRunning || m_reentrancyGuard) {
        EventDispatcherPrivate::get(m_eventDispatcher)->removeTimer(this);
    }

    if (m_reentrancyGuard) {
        *m_reentrancyGuard = false;
        m_reentrancyGuard = nullptr;
    }
}

void Timer::start(int msec)
{
    if (msec < 0) {
        std::cerr << "Timer::start(): interval cannot be negative!\n";
    }
    // restart if already running
    if (!m_reentrancyGuard && m_isRunning) {
        EventDispatcherPrivate::get(m_eventDispatcher)->removeTimer(this);
    }
    m_interval = msec;
    m_isRunning = true;
    if (!m_reentrancyGuard) {
        EventDispatcherPrivate::get(m_eventDispatcher)->addTimer(this);
    }
}

void Timer::stop()
{
    setRunning(false);
}

void Timer::setRunning(bool run)
{
    if (m_isRunning == run) {
        return;
    }
    m_isRunning = run;
    if (!m_reentrancyGuard) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        if (run) {
            ep->addTimer(this);
        } else {
            ep->removeTimer(this);
        }
    }
}

bool Timer::isRunning() const
{
    return m_isRunning;
}

void Timer::setInterval(int msec)
{
    if (msec < 0) {
        std::cerr << "Timer::setInterval(): interval cannot be negative!\n";
    }
    if (m_interval == msec) {
        return;
    }
    m_interval = msec;
    if (m_isRunning && !m_reentrancyGuard) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        ep->removeTimer(this);
        ep->addTimer(this);
    }
}

int Timer::interval() const
{
    return m_interval;
}

void Timer::setRepeating(bool repeating)
{
    m_isRepeating = repeating;
}

bool Timer::isRepeating() const
{
    return m_isRepeating;
}

int Timer::remainingTime() const
{
    if (!m_isRunning) {
        return -1;
    }
    const uint64 currentTime = PlatformTime::monotonicMsecs();
    if (currentTime > m_nextDueTime) {
        return 0;
    }
    return std::min(uint64(std::numeric_limits<int>::max()), m_nextDueTime - currentTime);
}

#if defined __GNUC__ && __GNUC__ >= 12
#define GCC_12
#endif

#ifdef GCC_12
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
void Timer::trigger()
{
    assert(m_isRunning);
    if (m_reentrancyGuard) {
        return;
    }
    if (!m_isRepeating) {
        m_isRunning = false;
    }

    // Changes to this timer while in the callback require special treatment. m_reentrancyGuard
    // helps provide that.
    bool alive = true;
    m_reentrancyGuard = &alive;
    if (m_completionListener) {
        m_completionListener->handleCompletion(this);
    }
    // if we we've been destroyed, we don't touch the member variable
    if (alive) {
        assert(m_reentrancyGuard);
        m_reentrancyGuard = nullptr;
    }
}
#ifdef GCC_12
#pragma GCC diagnostic pop
#endif

void Timer::setCompletionListener(ICompletionListener *client)
{
    m_completionListener = client;
}

ICompletionListener *Timer::completionClient() const
{
    return m_completionListener;
}

EventDispatcher *Timer::eventDispatcher() const
{
    return m_eventDispatcher;
}
