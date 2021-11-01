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

#include "iioeventsource.h"
#include "message.h"
#include "platform.h"
#include "spinlock.h"
#include "types.h"

#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

struct Event;
class IIoEventListener;
class IEventPoller;
class Message;
class PendingReplyPrivate;
class Timer;
class ConnectionPrivate;

// note that the main purpose of EventDispatcher so far is dispatching I/O events; dispatching Event
// instances is secondary
class EventDispatcherPrivate : public IIoEventSource
{
public:
    static EventDispatcherPrivate *get(EventDispatcher *ed) { return ed->d; }

    ~EventDispatcherPrivate();

    int timeToFirstDueTimer() const;
    uint nextTimerSerial();
    void tryCompactTimerSerials();
    void triggerDueTimers();

protected:
    // IIoEventSource
    void addIoListenerInternal(IIoEventListener *iol, uint32 ioRw) override;
    void removeIoListenerInternal(IIoEventListener *iol) override;
    void updateIoInterestInternal(IIoEventListener *iol, uint32 ioRw) override;

public:

    bool addIoEventListener(IIoEventListener *iol);
    bool removeIoEventListener(IIoEventListener *iol);
    void setReadWriteInterest(IIoEventListener *iol, bool read, bool write);
    // for IEventPoller
    friend class IEventPoller;
    void notifyListenerForIo(FileDescriptor fd, IO::RW ioRw);
    // for Timer
    friend class Timer;
    void addTimer(Timer *timer);
    void removeTimer(Timer *timer);
    // for ForeignEventLoopIntegrator (calls into it, not called from it)
    void maybeSetTimeoutForIntegrator();
    // for Connection
    // this is similar to interrupt(), but doesn't make poll() return false and will call
    // m_connectionToNotify -> processQueuedEvents()
    void wakeForEvents();
    void queueEvent(std::unique_ptr<Event> evt); // safe to call from any thread
    void processAuxEvents();

    IEventPoller *m_poller = nullptr;
    ForeignEventLoopIntegrator *m_integrator = nullptr;
    std::unordered_map<FileDescriptor, IIoEventListener*> m_ioListeners;

    // Attention! When changing s_maxTimerSerial, or the general approach to ensuring that timers time out
    // in the correct order, make sure that testSerialWraparound() still tests the ordering technique where
    // it's likely to break.
    static const int s_maxTimerSerial = 0x3ff; // 10 bits set
    uint m_currentTimerSerial = 0;
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
    // for inter thread event delivery to Connection
    ConnectionPrivate *m_connectionToNotify = nullptr;

    Spinlock m_queuedEventsLock;
    std::vector<std::unique_ptr<Event>> m_queuedEvents;
};

#endif
