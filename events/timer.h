// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef TIMER_H
#define TIMER_H

#include "types.h"

class EventDispatcher;
class EventDispatcherPrivate;
class ICompletionListener;

class DFERRY_EXPORT Timer
{
public:
    Timer(EventDispatcher *dispatcher);
    ~Timer();
    // can't allow copying because Timers are remembered by pointer in EventDispatcher
    // (and since this isn't a value class, copying makes little sense)
    Timer(const Timer &) = delete;
    void operator=(const Timer &) = delete;

    void start(int msec); // convenience: setInterval(msec) and setRunning(true)
    void stop(); // convenience: setRunning(false)
    void setRunning(bool);
    bool isRunning() const;

    void setInterval(int msec);
    int interval() const;

    void setRepeating(bool);
    bool isRepeating() const;

    int remainingTime() const;

    void setCompletionListener(ICompletionListener *client);
    ICompletionListener *completionListener() const;

    EventDispatcher *eventDispatcher() const;

private:
    friend class EventDispatcherPrivate;
    void trigger();
    EventDispatcher *m_eventDispatcher; // TODO make a per-thread event dispatcher implicit?
    ICompletionListener *m_completionListener;
    bool *m_reentrancyGuard;
    int m_interval;
    bool m_isRunning : 1;
    bool m_isRepeating : 1;
    uint32 m_reserved : sizeof(uint32) - 2;
    inline uint64 tag() { return (m_nextDueTime << 10) + m_serial; }
    uint64 m_nextDueTime : 54;
    uint32 m_serial : 10;
};

#endif // TIMER_H
