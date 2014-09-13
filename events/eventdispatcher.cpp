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

#include "epolleventpoller.h"
#include "iconnection.h"
#include "ieventpoller.h"
#include "platformtime.h"
#include "timer.h"

#include <algorithm>
#include <cassert>

//#define EVENTDISPATCHER_DEBUG

using namespace std;

EventDispatcher::EventDispatcher()
{
    // TODO other backend on other platforms
    m_poller = new EpollEventPoller(this);
}

EventDispatcher::~EventDispatcher()
{
    map<FileDescriptor, IConnection*>::iterator it = m_connections.begin();
    for ( ; it != m_connections.end(); ++it ) {
        it->second->setEventDispatcher(0);
    }

    multimap<uint64 /* due */, Timer*>::iterator tt = m_timers.begin();
    for ( ; tt != m_timers.end(); ++tt ) {
        tt->second->m_eventDispatcher = 0;
        tt->second->m_isRunning = false;
    }

    delete m_poller;
}

bool EventDispatcher::poll(int timeout)
{
    int nextDue = timeToFirstDueTimer();
    if (timeout < 0) {
        timeout = nextDue;
    } else if (nextDue >= 0) {
        timeout = std::min(timeout, nextDue);
    }

#ifdef EVENTDISPATCHER_DEBUG
    printf("EventDispatcher::poll(): timeout=%d, nextDue=%d.\n", timeout, nextDue);
#endif
    if (!m_poller->poll(timeout)) {
        return false;
    }
    triggerDueTimers();
    return true;
}

void EventDispatcher::interrupt()
{
    m_poller->interrupt();
}

bool EventDispatcher::addConnection(IConnection *conn)
{
    pair<map<FileDescriptor, IConnection*>::iterator, bool> insertResult;
    insertResult = m_connections.insert(make_pair(conn->fileDescriptor(), conn));
    const bool ret = insertResult.second;
    if (ret) {
        m_poller->addConnection(conn);
    }
    return ret;
}

bool EventDispatcher::removeConnection(IConnection *conn)
{
    const bool ret = m_connections.erase(conn->fileDescriptor());
    if (ret) {
        m_poller->removeConnection(conn);
    }
    return ret;
}

void EventDispatcher::setReadWriteInterest(IConnection *conn, bool read, bool write)
{
    m_poller->setReadWriteInterest(conn, read, write);
}

void EventDispatcher::notifyConnectionForReading(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

void EventDispatcher::notifyConnectionForWriting(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyWrite();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyWrite(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

int EventDispatcher::timeToFirstDueTimer() const
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

uint EventDispatcher::nextTimerSerial()
{
    if (++m_lastTimerSerial > s_maxTimerSerial) {
        m_lastTimerSerial = 0;
    }
    return m_lastTimerSerial;
}

void EventDispatcher::addTimer(Timer *timer)
{
    if (timer == m_triggeredTimer) {
        m_isTriggeredTimerPendingRemoval = false;
        return;
    }
    if (timer->m_tag == 0) {
        timer->m_tag = nextTimerSerial();
    }

    uint64 dueTime = PlatformTime::monotonicMsecs() + timer->m_interval;

    // ### When a timer is added from a timer callback, make sure it only runs in the *next*
    //     iteration of the event loop. Otherwise, endless cascades of timers triggering, adding
    //     more timers etc could occur without ever returning from triggerDueTimers().
    //     For the condition for this hazard, see "invariant:" in triggerDueTimers(): the only way
    //     the new timer could trigger in this event loop iteration is when:
    //
    //     m_triggerTime == currentTime(before call to trigger()) == timerAddedInTrigger().dueTime
    //
    //     note: m_triggeredTimer.dueTime < mtriggerTime is well possible; if ==, the additional
    //           condition applies that timerAddedInTrigger().serial >= m_triggeredTimer.serial;
    //           we ignore this and do it conservative and less complicated.
    //           (the additional condition stems from serials and how multimap insertion works)
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

void EventDispatcher::removeTimer(Timer *timer)
{
    assert(timer->m_tag != 0);

    if (timer == m_triggeredTimer) {
        // using this variable, we can avoid dereferencing m_triggeredTimer should it have been
        // removed from its destructor or otherwise removed and deleted while triggered
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

void EventDispatcher::triggerDueTimers()
{
    m_triggerTime = PlatformTime::monotonicMsecs();
    for (auto it = m_timers.begin(); it != m_timers.end();) {
        const uint64 timerTimeout = (it->first >> 10);
        if (timerTimeout > m_triggerTime) {
            break;
        }
        // careful here - protect against adding and removing any timers while inside trigger()!
        // we do this by keeping the iterator at the current position (so changing any other timer
        // doesn't invalidate it) and blocking changes to the timer behind that iterator
        // (so we don't mess with its data should it have been deleted outright in the callback)

        m_triggeredTimer = it->second;
        Timer *const timer = m_triggeredTimer;
        m_isTriggeredTimerPendingRemoval = false;

        // invariant: m_triggeredTimer.dueTime <= m_triggerTime <= currentTime(here) <= timerAddedInTrigger().dueTime
        timer->trigger();

        m_triggeredTimer = nullptr;
        if (!m_isTriggeredTimerPendingRemoval && timer->m_isRunning) {
            // ### we are rescheduling timers based on triggerTime even though real time can be much later - is
            // this the desired behavior? I think so...
            if (timer->m_interval == 0) {
                // we might iterate over this timer again in this invocation because we only break out of the
                // loop if timerTimeout > m_triggerTime, so just leave it behind - zero interval timers with
                // due time in the past work just fine in practice!
                ++it;
            } else {
                timer->m_tag = ((m_triggerTime + timer->m_interval) << 10) + (timer->m_tag & s_maxTimerSerial);
                m_timers.erase(it++);
                m_timers.emplace(timer->m_tag, timer);
            }
        } else {
            m_timers.erase(it++);
        }
    }
    m_triggerTime = 0;
}
