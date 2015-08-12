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

#include "epolleventpoller.h"
#include "event.h"
#include "ieventpoller.h"
#include "iioeventclient.h"
#include "platformtime.h"
#include "transceiver_p.h"
#include "timer.h"

#include <algorithm>
#include <cassert>

#include <iostream>

//#define EVENTDISPATCHER_DEBUG

using namespace std;

EventDispatcher::EventDispatcher()
   : d(new EventDispatcherPrivate)
{
    // TODO other backend on other platforms
    d->m_poller = new EpollEventPoller(this);
}

EventDispatcherPrivate::~EventDispatcherPrivate()
{
    for (const pair<FileDescriptor, IioEventClient*> &fdCon : m_ioClients) {
        fdCon.second->setEventDispatcher(0);
    }

    for (const pair<uint64 /* due */, Timer*> &dt : m_timers) {
        dt.second->m_eventDispatcher = 0;
        dt.second->m_isRunning = false;
    }

    delete m_poller;
}

EventDispatcher::~EventDispatcher()
{
    delete d;
    d = 0;
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
    } else if (interrupAction == IEventPoller::ProcessAuxEvents && d->m_transceiverToNotify) {
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

bool EventDispatcherPrivate::addIoEventClient(IioEventClient *ioc)
{
    pair<unordered_map<FileDescriptor, IioEventClient*>::iterator, bool> insertResult;
    insertResult = m_ioClients.insert(make_pair(ioc->fileDescriptor(), ioc));
    const bool ret = insertResult.second;
    if (ret) {
        m_poller->addIoEventClient(ioc);
    }
    return ret;
}

bool EventDispatcherPrivate::removeIoEventClient(IioEventClient *ioc)
{
    const bool ret = m_ioClients.erase(ioc->fileDescriptor());
    if (ret) {
        m_poller->removeIoEventClient(ioc);
    }
    return ret;
}

void EventDispatcherPrivate::setReadWriteInterest(IioEventClient *ioc, bool read, bool write)
{
    m_poller->setReadWriteInterest(ioc, read, write);
}

void EventDispatcherPrivate::notifyClientForReading(FileDescriptor fd)
{
    unordered_map<int, IioEventClient *>::iterator it = m_ioClients.find(fd);
    if (it != m_ioClients.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

void EventDispatcherPrivate::notifyClientForWriting(FileDescriptor fd)
{
    unordered_map<int, IioEventClient *>::iterator it = m_ioClients.find(fd);
    if (it != m_ioClients.end()) {
        it->second->notifyWrite();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyWrite(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

int EventDispatcherPrivate::timeToFirstDueTimer() const
{
    if (m_timers.empty()) {
        return -1;
    }
    uint64 nextTimeout = (*m_timers.cbegin()).first >> 10;
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
    if (timer == m_triggeredTimer) {
        m_isTriggeredTimerPendingRemoval = false;
        return;
    }
    if (timer->m_tag == 0) {
        timer->m_tag = nextTimerSerial();
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
    //           we ignore this and do it conservative and less complicated.
    //           (the additional condition comes from serials as keys and that each "slot" in multimap with
    //           the same keys is a list where new entries are back-inserted)
    //
    //     As a countermeasure, tweak the new timer's timeout, putting it well before m_triggeredTimer's
    //     iterator position in the multimap... because the new timer must have zero timeout in order for
    //     its due time to occur within this triggerDueTimers() iteration, it is supposed to trigger ASAP
    //     anyway. This disturbs the order of triggering a little compared to the usual, but all
    //     timeouts are properly respected - the next event loop iteration is guaranteed to trigger
    //     timers at times strictly greater-equal than this iteration ;)
    if (m_triggeredTimer && dueTime == m_triggerTime) {
        dueTime = (m_triggeredTimer->m_tag >> 10) - 1;
    }
    timer->m_tag = (dueTime << 10) + (timer->m_tag & s_maxTimerSerial);

    m_timers.emplace(timer->m_tag, timer);
}

void EventDispatcherPrivate::removeTimer(Timer *timer)
{
    assert(timer->m_tag != 0);

    if (timer == m_triggeredTimer) {
        // using this variable, we can avoid dereferencing m_triggeredTimer should it have been
        // deleted while triggered
        m_isTriggeredTimerPendingRemoval = true;
        return;
    }

    auto iterRange = m_timers.equal_range(timer->m_tag);
    for (; iterRange.first != iterRange.second; ++iterRange.first) {
        if (iterRange.first->second == timer) {
            m_timers.erase(iterRange.first);
            return;
        }
    }
    assert(false); // the timer should never request a remove when it has not been added
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
                timer->m_tag = ((m_triggerTime + uint64(timer->m_interval)) << 10) +
                               (timer->m_tag & s_maxTimerSerial);
                m_timers.erase(it++);
                m_timers.emplace(timer->m_tag, timer);
            }
        } else {
            m_timers.erase(it++);
        }
    }
    m_triggerTime = 0;
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
    if (m_transceiverToNotify) {
        for (const std::unique_ptr<Event> &evt : events) {
            m_transceiverToNotify->processEvent(evt.get());
        }
    }
}
