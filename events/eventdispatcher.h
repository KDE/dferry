// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef EVENTDISPATCHER_H
#define EVENTDISPATCHER_H

#include "export.h"

class EventDispatcherPrivate;
class ForeignEventLoopIntegrator;

class DFERRY_EXPORT EventDispatcher
{
public:
#ifndef DFERRY_NO_NATIVE_POLL
    EventDispatcher();
#endif
    EventDispatcher(ForeignEventLoopIntegrator *integrator);
    ~EventDispatcher();
    EventDispatcher(EventDispatcher &other) = delete;
    void operator=(EventDispatcher &other) = delete;

    bool poll(int timeout = -1);
    void interrupt();

    void run();

private:
    friend class EventDispatcherPrivate;
    EventDispatcherPrivate *d;
};

#endif // EVENTDISPATCHER_H
