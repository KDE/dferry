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
#include "icompletionclient.h"
#include "platformtime.h"

#include <algorithm>
#include <cassert>
#include <iostream>

Timer::Timer(EventDispatcher *dispatcher)
   : m_eventDispatcher(dispatcher),
     m_completionClient(0),
     m_reentrancyGuard(0),
     m_interval(0),
     m_isRunning(false),
     m_isRepeating(true),
     m_tag(0)
{
}

Timer::~Timer()
{
    if (m_reentrancyGuard) {
        *m_reentrancyGuard = false;
        m_reentrancyGuard = nullptr;
    }
    if (m_isRunning) {
        EventDispatcherPrivate::get(m_eventDispatcher)->removeTimer(this);
    }
}

void Timer::start(int msec)
{
    if (msec < 0) {
        std::cerr << "Timer::start(): interval cannot be negative!\n";
    }
    // restart if already running
    EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
    if (m_isRunning) {
        ep->removeTimer(this);
    }
    m_interval = msec;
    m_isRunning = true;
    ep->addTimer(this);
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
    EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
    if (run) {
        ep->addTimer(this);
    } else {
        ep->removeTimer(this);
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
    if (m_isRunning) {
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
    uint64 currentTime = PlatformTime::monotonicMsecs();
    return std::max(int((m_tag >> 10) - currentTime), 0);
}

void Timer::trigger()
{
    assert(m_isRunning);
    if (m_reentrancyGuard) {
        return;
    }
    if (!m_isRepeating) {
        m_isRunning = false;
    }

    // ### Reentrancy is not *currently* an issue, but when we have stuff like sub event loops,
    // we need this. Also in similar event-driven classes - here is how I think it should be done...
    bool alive = true;
    m_reentrancyGuard = &alive;
    if (m_completionClient) {
        m_completionClient->handleCompletion(this);
    }
    // if we we've been destroyed, we don't touch the member variable
    if (alive) {
        assert(m_reentrancyGuard);
        m_reentrancyGuard = nullptr;
    }
}

void Timer::setCompletionClient(ICompletionClient *client)
{
    m_completionClient = client;
}

ICompletionClient *Timer::completionClient() const
{
    return m_completionClient;
}

EventDispatcher *Timer::eventDispatcher() const
{
    return m_eventDispatcher;
}
