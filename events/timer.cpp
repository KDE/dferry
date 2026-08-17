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
#include "platformtime_p.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

/** \class Timer
    Notifies when a set amount of time has passed.

    This class is used internally by Dferry, but may be used for general-purpose waiting.

    \see EventDispatcher
*/

/// Constructs a Timer using EventDispatcher \p dispatcher.
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
        EventDispatcherPrivate::of(m_eventDispatcher)->removeTimer(this);
    }

    if (m_reentrancyGuard) {
        *m_reentrancyGuard = false;
        m_reentrancyGuard = nullptr;
    }
}

/// Starts the timer to trigger in \p msec milliseconds.
void Timer::start(int msec)
{
    if (msec < 0) {
        std::cerr << "Timer::start(): interval cannot be negative!\n";
        return;
    }
    // restart if already running
    if (!m_reentrancyGuard && m_isRunning) {
        EventDispatcherPrivate::of(m_eventDispatcher)->removeTimer(this);
    }
    m_interval = msec;
    m_isRunning = true;
    if (!m_reentrancyGuard) {
        EventDispatcherPrivate::of(m_eventDispatcher)->addTimer(this);
    }
}

/// Stops the timer.
void Timer::stop()
{
    setRunning(false);
}

/// Starts or stops the timer.
/// This will not restart the timer if it is already running and \p run is true.
void Timer::setRunning(bool run)
{
    if (m_isRunning == run) {
        return;
    }
    m_isRunning = run;
    if (!m_reentrancyGuard) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::of(m_eventDispatcher);
        if (run) {
            ep->addTimer(this);
        } else {
            ep->removeTimer(this);
        }
    }
}

/// \returns whether the timer is running.
bool Timer::isRunning() const
{
    return m_isRunning;
}

/// Sets the interval of the timer in milliseconds.
/// This will restart the timer with the new interval if it is already running.
void Timer::setInterval(int msec)
{
    if (msec < 0) {
        std::cerr << "Timer::setInterval(): interval cannot be negative!\n";
        return;
    }
    m_interval = msec;
    if (m_isRunning && !m_reentrancyGuard) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::of(m_eventDispatcher);
        ep->removeTimer(this);
        ep->addTimer(this);
    }
}

/// \returns the interval of the timer in milliseconds.
int Timer::interval() const
{
    return m_interval;
}

/// Sets whether this timmer will trigger repeatedly.
/// Otherwise, it will trigger only once and then stop.
void Timer::setRepeating(bool repeating)
{
    m_isRepeating = repeating;
}

/// \returns whether this timmer will trigger repeatedly.
/// Otherwise, it will trigger only once and then stop.
bool Timer::isRepeating() const
{
    return m_isRepeating;
}

/// \returns The number of milliseconds left until the timer triggers.
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
    // if we've been destroyed, don't touch any member variables
    if (alive) {
        assert(m_reentrancyGuard);
        m_reentrancyGuard = nullptr;
    }
}
#ifdef GCC_12
#pragma GCC diagnostic pop
#endif

/// Sets the listener that will be invoked when the timer triggers.
void Timer::setCompletionListener(ICompletionListener *client)
{
    m_completionListener = client;
}

/// \returns the listener that will be invoked when the timer triggers.
ICompletionListener *Timer::completionListener() const
{
    return m_completionListener;
}

/// \returns the EventDispatcher that was set in the constructor.
EventDispatcher *Timer::eventDispatcher() const
{
    return m_eventDispatcher;
}
