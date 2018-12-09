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

#ifndef IIOEVENTFORWARDER_H
#define IIOEVENTFORWARDER_H

#include "iioeventlistener.h"
#include "iioeventsource.h"

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

#endif // IIOEVENTFORWARDER_H
