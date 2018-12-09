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

#include "iioeventforwarder.h"

#include <cassert>

// The logic of add/removeIoListenerInternal is based on knowledge that one Connection or Server can only
// have one I/O listener, which is the one ITransport connecting to the bus or peer.
void IIoEventForwarder::addIoListenerInternal(IIoEventListener *iol, uint32 ioRw)
{
    assert(!ioEventSource()); // since our clients are internal, they are expected to be well-behaved
    assert(!m_downstream);
    setIoInterest(ioRw);
    m_downstream = iol;
    m_upstream->addIoListener(this);
    assert(ioEventSource());
    assert(m_upstream == ioEventSource());
}

void IIoEventForwarder::removeIoListenerInternal(IIoEventListener * /* iol */)
{
    assert(ioEventSource());
    assert(m_upstream == ioEventSource());
    assert(m_downstream);
    m_upstream->removeIoListener(this);
    m_downstream = nullptr;
    // (no need to change I/O interest, only upstream can see it and we have no upstream now)
    assert(!ioEventSource());
}

void IIoEventForwarder::updateIoInterestInternal(IIoEventListener * /*iol*/, uint32 ioRw)
{
    setIoInterest(ioRw);
}

FileDescriptor IIoEventForwarder::fileDescriptor() const
{
    return m_downstream->fileDescriptor();
}

IIoEventListener *IIoEventForwarder::downstreamListener()
{
    return m_downstream;
}

#if 0
// Sample implementation for subclasses
IO::Status IIoEventForwarderSubclass::handleIoReady(IO::RW rw)
{
    IO::Status result = m_ioEventSink->handleIoReady(rw);
    if (result != IO::Status::OK) {
        // error handling - connection teardown and notifying some other listeners, for example
    }
}
#endif
