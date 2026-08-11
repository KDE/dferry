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
