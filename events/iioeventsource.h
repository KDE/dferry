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

#ifndef IIOEVENTSOURCE_H
#define IIOEVENTSOURCE_H

#include "iovaluetypes.h"
#include "types.h"

class IIoEventListener;

// TODO: explain the context for this - layered sources and listeners and such

// There is one subtle problem that needs to be solved regarding IIoEventListener::fileDescriptor().
// Event sources are expected to use some kind of hash / map FileDescriptor -> IIoEventListener, so
// an IIoEventListener cannot unregister after closing the file descriptor. Even keeping the old
// value around doesn't help - the file descriptor could be reused very soon by any other part of the
// current process.
// So the lowest level implementation of close() must immediately unregister the IIoEventListener
// leading to the I/O operation leading to the close(). That lowest level IIoEventListener must also
// unregister itself, etc, up to the highest level IIoEventSource (usually EventDispatcherPrivate).

class IIoEventSource
{
public:
    virtual ~IIoEventSource();

    // Contract: an IIoEventSource may have different IIoEventListeners listening to read and write
    // As a further restriction, a listener must always remove I/O interest from its old source
    // before enabling it on its new source.
    void addIoListener(IIoEventListener *iol);
    void removeIoListener(IIoEventListener *iol);

protected:
    // add / remove only make sense for stateful APIs such as Linux epoll, so they don't have to be
    // implemented - the defaults just call updateIoInterestInternal().
    virtual void addIoListenerInternal(IIoEventListener *iol, uint32 ioRw);
    virtual void removeIoListenerInternal(IIoEventListener *iol);
    virtual void updateIoInterestInternal(IIoEventListener *iol, uint32 ioRw) = 0;

private:
    friend class IIoEventListener;
    // called from IIoEventListener when its m_ioInterest changed
    void updateIoInterest(IIoEventListener *iol);
};

#endif // IIOEVENTSOURCE_H
