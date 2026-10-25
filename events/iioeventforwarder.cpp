// SPDX-FileCopyrightText: 2018 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "iioeventforwarder_p.h"

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

#endif // RUNNING_DOXYGEN
