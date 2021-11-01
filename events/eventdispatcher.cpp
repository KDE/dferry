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
    if (it == m_timers.cend()) {
        return -1;
    }
    if (it->second == nullptr) {
        // this is the dead entry of the currently triggered, and meanwhile removed timer
        if (++it == m_timers.cend()) {
            return -1;
        }
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
    if (++m_lastTimerSerial > s_maxTimerSerial) {
        m_lastTimerSerial = 0;
    }
    return m_lastTimerSerial;
}

void EventDispatcherPrivate::addTimer(Timer *timer)
{
    if (timer->tag() == 0) {
        timer->m_serial = nextTimerSerial();
    }

    uint64 dueTime = PlatformTime::monotonicMsecs() + uint64(timer->m_interval);

    // ### When a timer is added from a timer callback, make sure it only runs in the *next*
    //     iteration of the event loop. Otherwise, endless cascades of timers triggering, adding
    //     more timers etc could occur without ever returning from triggerDueTimers().
    //     For the condition for this hazard, see "invariant:" in triggerDueTimers(): the only way
    //     the new timer could trigger in this event loop iteration is when:
    //
    //     m_triggerTime == currentTime(before call to trigger()) == timerAddedInTrigger().dueTime
    //
    //     note: m_triggeredTimer.dueTime < m_triggerTime is well possible; if ==, the additional
    //           condition applies that timerAddedInTrigger().serial >= m_triggeredTimer.serial;
    //           we ignore this and do it conservatively and less complicated.
    //           (the additional condition comes from serials as keys and that each "slot" in multimap with
    //           the same keys is a list where new entries are back-inserted)
    //
    //     As a countermeasure, tweak the new timer's timeout, putting it well before m_triggeredTimer's
    //     iterator position in the multimap... because the new timer must have zero timeout in order for
    //     its due time to occur within this triggerDueTimers() iteration, it is supposed to trigger ASAP
    //     anyway. This disturbs the order of triggering a little compared to the usual, but all
    //     timeouts are properly respected - the next event loop iteration is guaranteed to trigger
    //     timers at times strictly greater-equal than this iteration (time goes only one way) ;)
    if (m_triggerTime && dueTime == m_triggerTime) {
        dueTime = m_triggerTime - 1;
    }
    timer->m_nextDueTime = dueTime;

    m_timers.emplace(timer->tag(), timer);
    maybeSetTimeoutForIntegrator();
}

void EventDispatcherPrivate::removeTimer(Timer *timer)
{
    assert(timer->tag() != 0);

    // We cannot toggle m_isTriggeredTimerPendingRemoval back and forth, we can only set it once.
    // Because after the timer has been removed once, the next time we see the same pointer value,
    // it could be an entirely different timer. Consider this:
    // delete timer1; // calls removeTimer()
    // Timer *timer2 = new Timer(); // accidentally gets same memory address as timer1
    // timer2->start(...);
    // timer2->stop(); // timer == m_triggeredTimer, uh oh
    // The last line does not necessarily cause a problem, but just don't be excessively clever.
    // On the other hand, not special-casing the currently triggered timer after it has been marked
    // for removal once is fine.  In case it  is re-added, it gets a new map entry in addTimer()
    // and from then on it can be handled like any other timer.
    bool removingTriggeredTimer = false;
    if (!m_isTriggeredTimerPendingRemoval && timer == m_triggeredTimer) {
        // using this variable, we can avoid dereferencing m_triggeredTimer should it have been
        // deleted while triggered
        m_isTriggeredTimerPendingRemoval = true;
        removingTriggeredTimer = true;
    }

    auto iterRange = m_timers.equal_range(timer->tag());
    for (; iterRange.first != iterRange.second; ++iterRange.first) {
        if (iterRange.first->second == timer) {
            if (!removingTriggeredTimer) {
                m_timers.erase(iterRange.first);
            } else {
                // mark it as dead for query methods such as timeToFirstDueTimer()
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
        const uint64 timerTimeout = (it->first >> 10);
        if (timerTimeout > m_triggerTime) {
            break;
        }
        // careful here - protect against adding and removing any timer while inside its trigger()!
        // we do this by keeping the iterator at the current position (so changing any other timer
        // doesn't invalidate it) and blocking changes to the timer behind that iterator
        // (so we don't mess with its data should it have been deleted outright in the callback)

        m_triggeredTimer = it->second;
        Timer *const timer = m_triggeredTimer;
        m_isTriggeredTimerPendingRemoval = false;

        // invariant:
        // m_triggeredTimer.dueTime <= m_triggerTime <= currentTime(here) <= <timerAddedInTrigger>.dueTime
        timer->trigger();

        m_triggeredTimer = nullptr;
        if (!m_isTriggeredTimerPendingRemoval && timer->m_isRunning) {
            // ### we are rescheduling timers based on triggerTime even though real time can be
            // much later - is this the desired behavior? I think so...
            if (timer->m_interval == 0) {
                // With the other branch we might iterate over this timer again in this invocation because
                // if there are several timers with the same tag, this entry will be back-inserted into the
                // list of values for the current tag / key slot. We only break out of the loop if
                // timerTimeout > m_triggerTime, so there would be an infinite loop.
                // Instead, we just leave the iterator alone, which does not put it in front of the current
                // iterator position. It's also good for performance. Win-win!
                ++it;
            } else {
                timer->m_nextDueTime = m_triggerTime + timer->m_interval;
                m_timers.erase(it++);
                m_timers.emplace(timer->tag(), timer);
            }
        } else {
            m_timers.erase(it++);
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
