// SPDX-FileCopyrightText: 2018 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IIOEVENTFORWARDER_P_H
#define IIOEVENTFORWARDER_P_H

#include "iioeventlistener_p.h"
#include "iioeventsource_p.h"

// How to use:
// - construct with upstream source as constructor argument
// - connect listener: addIoListener(listener)
// - use a reimplementation of handleIoReady() to observe I/O
// - listener will remove itself automatically when it closes the I/O channel, which also causes
//   the forwarder to remove itself from the source (see removeIoListenerInternal() implementation)
// - it is possible to start over at this point, if needed (connect listener, etc)
// NOTE: This connects one source to ONE listener, not one to many like a generic IIoEventSource does.
//       More is not needed for the use cases of this class (Connection and Server).
class IIoEventForwarder : public IIoEventListener, public IIoEventSource
{
public:
    IIoEventForwarder(IIoEventSource *upstreamSource)
       : m_upstream(upstreamSource) {}

    // IO::Status IIOEventListener::handleIoReady(IO::RW rw) override;
    // not overridden - users of this class reimplement it to spy on and/or intercept I/O events!

    // IIOEventListener
    FileDescriptor fileDescriptor() const override;

    // This only works due to the one-to-one limitation explained above.
    IIoEventListener *downstreamListener();

protected:
    // IIOEventSource
    void addIoListenerInternal(IIoEventListener *iol, uint32 ioRw) override;
    void removeIoListenerInternal(IIoEventListener *iol) override;
    void updateIoInterestInternal(IIoEventListener *iol, uint32 ioRw) override;

private:
    IIoEventSource *m_upstream = nullptr;
    IIoEventListener *m_downstream = nullptr;
};

#endif // IIOEVENTFORWARDER_P_H
