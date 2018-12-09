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

#ifndef IIOEVENTLISTENER_H
#define IIOEVENTLISTENER_H

#include "platform.h"
#include "types.h"
#include "iovaluetypes.h"

class IIoEventSource;

class IIoEventListener
{
public:
    virtual ~IIoEventListener();

    // Contract: from the first to the last call from this class to IIoEventSource, the FileDescriptor
    // may not change. Notably, when closing, which involves resetting the file descriptor, the listener must
    // remove itself frome the IIoEventSource BEFORE resetting (or closing) the file descriptor.
    // (If the listener closed the fd but did not reset its value - i.e. what FileDescriptor() returns - ,
    // another part of the program could get the same numeric fd value, leading to clashes. So just
    // unregister before actually closing the fd.)

    IIoEventSource *ioEventSource() const;
    uint32 ioInterest() const;
    virtual IO::Status handleIoReady(IO::RW rw) = 0;
    virtual FileDescriptor fileDescriptor() const = 0;

protected:
    void setIoInterest(uint32 ioRw);

private:
    friend class IIoEventSource;
    IIoEventSource *m_eventSource = nullptr; // set by IIoEventSource, read by this class
    uint32 m_ioInterest = 0; // set by this class, read by IIoEventSource
};

#endif // IIOEVENTLISTENER_H
