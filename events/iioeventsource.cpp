/*
   Copyright (C) 2018 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "iioeventsource.h"

#include "iioeventlistener.h"

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
