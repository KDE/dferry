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

#include "eventdispatcher.h"
#include "eventdispatcher_p.h"

#ifndef DFERRY_NO_NATIVE_POLL
#ifdef __linux__
#include "epolleventpoller.h"
#elif defined _WIN32
#include "selecteventpoller_win32.h"
#else
#include "selecteventpoller_unix.h"
#endif
#endif

#include "event.h"
#include "foreigneventloopintegrator.h"
#include "ieventpoller.h"
#include "iioeventlistener.h"
#include "platformtime.h"
#include "connection_p.h"
#include "timer.h"

#include <algorithm>
#include <cassert>

#include <iostream>

//#define EVENTDISPATCHER_DEBUG

#ifndef DFERRY_NO_NATIVE_POLL
EventDispatcher::EventDispatcher()
   : d(new EventDispatcherPrivate)
{
#ifdef __linux__
    d->m_poller = new EpollEventPoller(this);
#else
    // TODO high performance IO multiplexers for non-Linux platforms
    d->m_poller = new SelectEventPoller(this);
#endif
}
#endif

EventDispatcher::EventDispatcher(ForeignEventLoopIntegrator *integrator)
   : d(new EventDispatcherPrivate)
{
    d->m_integrator = integrator;
    d->m_poller = integrator->connectToDispatcher(this);
}

EventDispatcherPrivate::~EventDispatcherPrivate()
{
    // removeIoListener is going to remove the current entry from m_ioListeners, so use this funny
    // way to "iterate"...
    for (auto it = m_ioListeners.cbegin(); it != m_ioListeners.cend(); it = m_ioListeners.cbegin())
    {
        const size_t sizeBefore = m_ioListeners.size();
        removeIoListener(it->second);
        if (m_ioListeners.size() == sizeBefore)
        {
            // this should never happen, however, avoid an infinite loop if it somehow does...
            assert(false);
            m_ioListeners.erase(it);
        }
    }

    for (auto it = m_timers.cbegin(); it != m_timers.cend(); ++it) {
        it->second->m_eventDispatcher = nullptr;
        it->second->m_isRunning = false;
    }

    if (!m_integrator) {
        delete m_poller;
    }
}

EventDispatcher::~EventDispatcher()
{
    delete d;
    d = nullptr;
}

bool EventDispatcher::poll(int timeout)
{
    int nextDue = d->timeToFirstDueTimer();
    if (timeout < 0) {
        timeout = nextDue;
    } else if (nextDue >= 0) {
        timeout = std::min(timeout, nextDue);
    }

#ifdef EVENTDISPATCHER_DEBUG
    printf("EventDispatcher::poll(): timeout=%d, nextDue=%d.\n", timeout, nextDue);
#endif
    IEventPoller::InterruptAction interrupAction = d->m_poller->poll(timeout);

    if (interrupAction == IEventPoller::Stop) {
        return false;
    } else if (interrupAction == IEventPoller::ProcessAuxEvents && d->m_connectionToNotify) {
        d->processAuxEvents();
    }
    d->triggerDueTimers();
    return true;
}

void EventDispatcher::interrupt()
{
    d->m_poller->interrupt(IEventPoller::Stop);
}

void EventDispatcherPrivate::wakeForEvents()
{
    m_poller->interrupt(IEventPoller::ProcessAuxEvents);
}

void EventDispatcherPrivate::addIoListenerInternal(IIoEventListener *iol, uint32 ioRw)
{
    std::pair<std::unordered_map<FileDescriptor, IIoEventListener*>::iterator, bool> insertResult;
    insertResult = m_ioListeners.insert(std::make_pair(iol->fileDescriptor(), iol));
    if (insertResult.second) {
        m_poller->addFileDescriptor(iol->fileDescriptor(), ioRw);
    }
}

void EventDispatcherPrivate::removeIoListenerInternal(IIoEventListener *iol)
{
    if (m_ioListeners.erase(iol->fileDescriptor())) {
        m_poller->removeFileDescriptor(iol->fileDescriptor());
    }
}

void EventDispatcherPrivate::updateIoInterestInternal(IIoEventListener *iol, uint32 ioRw)
{
    m_poller->setReadWriteInterest(iol->fileDescriptor(), ioRw);
}

void EventDispatcherPrivate::notifyListenerForIo(FileDescriptor fd, IO::RW ioRw)
{
    std::unordered_map<FileDescriptor, IIoEventListener *>::iterator it = m_ioListeners.find(fd);
    if (it != m_ioListeners.end()) {
        it->second->handleIoReady(ioRw);
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its handleCanRead() or handleCanWrite() implementation
        std::cerr << "EventDispatcherPrivate::notifyListenerForIo(): unhandled file descriptor "
                  <<  fd << ".\n";
#endif
    }
}

int EventDispatcherPrivate::timeToFirstDueTimer() const
{
    std::multimap<uint64, Timer*>::const_iterator it = m_timers.cbegin();
    if (it != m_timers.cend() && it->second == nullptr) {
        // this is the dead entry of the currently triggered, currently being deleted Timer
        ++it;
    }

    if (it == m_timers.cend()) {
        return -1;
    }

    uint64 nextTimeout = it->first >> 10;
    uint64 currentTime = PlatformTime::monotonicMsecs();

    if (currentTime >= nextTimeout) {
        return 0;
    }
    return nextTimeout - currentTime;
}

uint EventDispatcherPrivate::nextTimerSerial()
{
    if (m_currentTimerSerial > s_maxTimerSerial) {
        tryCompactTimerSerials();
    }
    return m_currentTimerSerial++;
}

void EventDispatcherPrivate::tryCompactTimerSerials()
{
    // don't bother trying to compact when there won't be much room anyway (we are probably heading for an
    // unavoidable overflow / duplicates)
    const size_t timersCount = m_timers.size();
    if (timersCount >= s_maxTimerSerial * 0.9) {
        std::cerr << timersCount << "are too many active timers! Timers timing out at the same time are "
                                    "not guaranteed to trigger in a predictable order anymore.\n";
        m_currentTimerSerial = 0;
        return;
    }

    bool pickNextTimer = false;
    int numDeadEntries = 0;
    auto it = m_timers.begin();
    for (uint newSerial = 0; newSerial < timersCount; newSerial++) {
        Timer *const timer = it->second;
        it = m_timers.erase(it);
        if (!pickNextTimer) {
            if (timer && timer->m_isRunning) {
                timer->m_serial = newSerial;
                m_timers.emplace(timer->tag(), timer);
            } else {
                // Drop this timer (it was removed while triggered) and use the serial for the next timer.
                // In that situation, we are being called from inside that timer's callback. Prepare for
                // iteration in triggerDueTimers() to continue after the current timer.
                newSerial--;
                m_adjustedIteratorOfNextTimer = m_timers.end();
                numDeadEntries++;
                pickNextTimer = true;
            }
        } else {
            // Only one timer can be currently triggered, which produces a "dead" map entry. Any other
            // timers are either running, or have already been removed from the map. Check this here.
            assert(timer);
            assert(timer->m_isRunning);
            timer->m_serial = newSerial;
            m_adjustedIteratorOfNextTimer = m_timers.emplace(timer->tag(), timer);
            pickNextTimer = false;
        }
    }

    assert(numDeadEntries <= 1);

    m_serialsCompacted = true;
    m_currentTimerSerial = timersCount;
}

void EventDispatcherPrivate::printTimerMap() const
{
    for (auto it = m_timers.cbegin(); it != m_timers.cend(); ++it) {
        std::cerr << "tag: " << it->first
                  << " dueTime: " << (it->first >> 10) << " serial: " << (it->first & 0x3ff)
                  << " pointer: " << it->second << '\n';
    }
}

void EventDispatcherPrivate::addTimer(Timer *timer)
{
    timer->m_serial = nextTimerSerial();


    uint64 dueTime;
    if (timer->m_interval != 0 || !m_triggerTime) {
        //std::cerr << "addTimer regular path " << timer << '\n';
        dueTime = PlatformTime::monotonicMsecs() + uint64(timer->m_interval);
    } else {
        // A timer is added from a timer callback - make sure it only runs in the *next* iteration
        // of the event loop. Timer users expect a timer to run at the earliest when the event loop
        // runs *again*, not in this iteration.
        //std::cerr << "addTimer staging path " << timer << '\n';
        dueTime = 0;
    }

    timer->m_nextDueTime = dueTime;

    //std::cerr << "  addTimer before:\n";
    //printTimerMap();
    m_timers.emplace(timer->tag(), timer);
    maybeSetTimeoutForIntegrator();
    //std::cerr << "  addTimer after:\n";
    //printTimerMap();
}

void EventDispatcherPrivate::removeTimer(Timer *timer)
{
    assert(timer->tag() != 0);

    // If inside a timer instance T's callback, this is only called from T's destructor, never from
    // T.setRunning(false). In the setRunning(false) case, removing is handled in triggerDueTimers()
    // right after invoking the callback by looking at T.m_isRunning. In the destructor case, this
    // sets the Timer pointer to nullptr (see below).
    // It is possible that the technique for handling the destructor case could also handle the
    // setRunning(false) case, something to consider... (Note: tryCompactTimerSerials kinda does that
    // already.)
    auto iterRange = m_timers.equal_range(timer->tag());
    for (; iterRange.first != iterRange.second; ++iterRange.first) {
        if (iterRange.first->second == timer) {
            if (!timer->m_reentrancyGuard) {
                m_timers.erase(iterRange.first);
            } else {
                // This means that this is an "emergency remove" of a timer being deleted while in its
                // callback. Mark it as dead so we won't dereference it. The map entry will be erased
                // in triggerDueTimers() shortly after the callback returns.
                iterRange.first->second = nullptr;
            }
            maybeSetTimeoutForIntegrator();
            return;
        }
    }
    assert(false); // the timer should never request a remove when it has not been added
}

void EventDispatcherPrivate::maybeSetTimeoutForIntegrator()
{
    if (m_integrator) {
        m_integrator->watchTimeout(timeToFirstDueTimer());
    }
}

void EventDispatcherPrivate::triggerDueTimers()
{
    m_triggerTime = PlatformTime::monotonicMsecs();

    for (auto it = m_timers.begin(); it != m_timers.end();) {
        const uint64 timerTimeout = it->first >> 10;
        if (timerTimeout > m_triggerTime) {
            break;
        }
        // careful here - protect against adding and removing any timer while inside its trigger()!
        // we do this by keeping the iterator at the current position (so changing any other timer
        // doesn't invalidate it) and blocking changes to the timer behind that iterator
        // (so we don't mess with its data should it have been deleted outright in the callback)

        m_serialsCompacted = false;

        Timer *timer = it->second;
        //std::cerr << "triggerDueTimers() - tag: " << it->first <<" pointer: " << timer << '\n';
        assert(timer->m_isRunning);

        // invariant:
        // timer.dueTime <= m_triggerTime <= currentTime(here) <= <timerAddedInTrigger>.dueTime
        // (the latter except for zero interval timers added in a timer callback, which go into
        // the staging area)
        if (timer->m_nextDueTime != 0) { // == 0: timer is in staging area
            timer->trigger();
        }

        if (m_serialsCompacted)
        {
            it = m_adjustedIteratorOfNextTimer;
            continue;
        }

        timer = it->second; // reload, removeTimer() may set it to nullptr
        if (timer && timer->m_isRunning) {
            // ### we are rescheduling timers based on triggerTime even though real time can be
            // much later - is this the desired behavior? I think so...
            if (timer->m_interval == 0 && timer->m_nextDueTime != 0) {
                // If we reinserted a timer with m_interval == 0, we might iterate over it again in this run
                // of triggerDueTimers(). If we just leave it where it is and keep iterating, we prevent
                // that problem, and it's good for performance, too!
                // If m_nextDueTime is zero, the timer was inserted during the last run of triggerDueTimers,
                // and it *should* be reinserted, so that the timer is triggered in this and future runs.
                ++it;
            } else {
                timer->m_nextDueTime = m_triggerTime + timer->m_interval;
                it = m_timers.erase(it);
                m_timers.emplace(timer->tag(), timer);
            }
        } else {
            it = m_timers.erase(it);
        }
    }
    m_triggerTime = 0;
    maybeSetTimeoutForIntegrator();
}

void EventDispatcherPrivate::queueEvent(std::unique_ptr<Event> evt)
{
    // std::cerr << "EventDispatcherPrivate::queueEvent() " << evt->type << " " << this << std::endl;
    {
        SpinLocker locker(&m_queuedEventsLock);
        m_queuedEvents.emplace_back(std::move(evt));
    }
    wakeForEvents();
}

void EventDispatcherPrivate::processAuxEvents()
{
    // std::cerr << "EventDispatcherPrivate::processAuxEvents() " << this << std::endl;
    // don't hog the lock while processing the events
    std::vector<std::unique_ptr<Event>> events;
    {
        SpinLocker locker(&m_queuedEventsLock);
        std::swap(events, m_queuedEvents);
    }
    if (m_connectionToNotify) {
        for (const std::unique_ptr<Event> &evt : events) {
            m_connectionToNotify->processEvent(evt.get());
        }
    }
}
