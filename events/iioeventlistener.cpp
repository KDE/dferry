// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "iioeventlistener_p.h"

#include "iioeventsource_p.h"

#include <cassert>

IIoEventListener::~IIoEventListener()
{
    // ### we would like to remove ourself from any IIoEventSource here, but we "are" only
    //     the base class at this point, so it can't be done here. However, we can check!
    assert(!m_eventSource);
#if 0
    if (m_eventSource) {
        // can't do this, it calls fileDescriptor() which is a pure virtual at this point
        m_eventSource->removeIoListener(this);
    }
#endif
}

IIoEventSource *IIoEventListener::ioEventSource() const
{
    return m_eventSource;
}

uint32 IIoEventListener::ioInterest() const
{
    return m_ioInterest;
}

void IIoEventListener::setIoInterest(uint32 ioRw)
{
    if (m_ioInterest == ioRw) {
        return;
    }
    m_ioInterest = ioRw;
    if (m_eventSource) {
        m_eventSource->updateIoInterest(this);
    }
}

#endif // RUNNING_DOXYGEN
