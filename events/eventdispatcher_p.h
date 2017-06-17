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

#ifndef EVENTDISPATCHER_P_H
#define EVENTDISPATCHER_P_H

#include "eventdispatcher.h"

#include "message.h"
#include "platform.h"
#include "spinlock.h"
#include "types.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

struct Event;
class IioEventClient;
class IEventPoller;
class Message;
class PendingReplyPrivate;
class Timer;
class TransceiverPrivate;

// note that the main purpose of EventDispatcher so far is dispatching I/O events; dispatching Event
// instances is secondary
class EventDispatcherPrivate
{
public:
    static EventDispatcherPrivate *get(EventDispatcher *ed) { return ed->d; }

    ~EventDispatcherPrivate();

    int timeToFirstDueTimer() const;
    uint nextTimerSerial();
    void triggerDueTimers();

    // for IioEventClient
    friend class IioEventClient;
    bool addIoEventClient(IioEventClient *ioc);
    bool removeIoEventClient(IioEventClient *ioc);
    void setReadWriteInterest(IioEventClient *ioc, bool read, bool write);
    // for IEventPoller
    friend class IEventPoller;
    void notifyClientForReading(FileDescriptor fd);
    void notifyClientForWriting(FileDescriptor fd);
    // for Timer
    friend class Timer;
    void addTimer(Timer *timer);
    void removeTimer(Timer *timer);
    // for ForeignEventLoopIntegrator (calls into it, not called from it)
    void maybeSetTimeoutForIntegrator();
    // for Transceiver
    // this is similar to interrupt(), but doesn't make poll() return false and will call
    // m_transceiverToNotify() -> processQueuedEvents()
    void wakeForEvents();
    void queueEvent(std::unique_ptr<Event> evt); // safe to call from any thread
    void processAuxEvents();

    IEventPoller *m_poller = nullptr;
    ForeignEventLoopIntegrator *m_integrator = nullptr;
    std::unordered_map<FileDescriptor, IioEventClient*> m_ioClients;

    static const int s_maxTimerSerial = 0x3ff; // 10 bits set
    uint m_lastTimerSerial = s_maxTimerSerial;
    // the highest 54 bits in "due" encode due time, the lowest 10 bits act like a serial number to reduce
    // (not eliminate - the serial eventually wraps around) collisions of timers with the same timeout
    // (this is not expressed as a struct/class to avoid compiler pessimization in the multimap code)
    std::multimap<uint64 /* due */, Timer*> m_timers;
    // for logic to prevent executing a timer in the dispatch run it was added
    uint64 m_triggerTime = 0;
    // helpers that help to avoid touching the currently triggered timer after it has been deleted in
    // a client called from trigger()
    Timer *m_triggeredTimer = nullptr;
    bool m_isTriggeredTimerPendingRemoval = false;
    // for inter thread event delivery to Transceiver
    TransceiverPrivate *m_transceiverToNotify = nullptr;

    Spinlock m_queuedEventsLock;
    std::vector<std::unique_ptr<Event>> m_queuedEvents;
};

#endif
