// SPDX-FileCopyrightText: 2018 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "iioeventsource_p.h"

#include "iioeventlistener_p.h"

#include <cassert>

IIoEventSource::~IIoEventSource()
{
    // ### we would like to tell all listeners they are unsubscribed here, but we "are" only
    //     the base class at this point, so it can't be done here.
}

void IIoEventSource::addIoListener(IIoEventListener *iol)
{
    if (iol->m_eventSource && iol->m_eventSource != this) {
        iol->m_eventSource->removeIoListener(iol);
    }
    assert(iol->m_eventSource == nullptr);
    iol->m_eventSource = this;
    addIoListenerInternal(iol, iol->m_ioInterest);
}

void IIoEventSource::removeIoListener(IIoEventListener *iol)
{
    if (iol->m_eventSource != this) {
        return; // warning?
    }
    iol->m_eventSource = nullptr;
    removeIoListenerInternal(iol);
}

void IIoEventSource::updateIoInterest(IIoEventListener *iol)
{
    if (iol->m_eventSource != this) {
        return; // warning?
    }
    updateIoInterestInternal(iol, iol->m_ioInterest);
}

void IIoEventSource::addIoListenerInternal(IIoEventListener *iol, uint32 ioRw)
{
    updateIoInterestInternal(iol, ioRw);
}

void IIoEventSource::removeIoListenerInternal(IIoEventListener *iol)
{
    updateIoInterestInternal(iol, 0);
}

#endif // RUNNING_DOXYGEN
