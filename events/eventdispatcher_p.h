// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef EVENTDISPATCHER_P_H
#define EVENTDISPATCHER_P_H

#include "eventdispatcher.h"

#include "iioeventsource_p.h"
#include "message.h"
#include "platform_p.h"
#include "spinlock_p.h"
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
    static EventDispatcherPrivate *of(EventDispatcher *ed) { return ed->d; }

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
    void printTimerMap() const;
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
    bool m_serialsCompacted = false;
    decltype(m_timers)::iterator m_adjustedIteratorOfNextTimer;

    // for inter thread event delivery to Connection
    ConnectionPrivate *m_connectionToNotify = nullptr;

    Spinlock m_queuedEventsLock;
    std::vector<std::unique_ptr<Event>> m_queuedEvents;
};

#endif
