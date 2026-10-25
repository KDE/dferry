// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IIOEVENTLISTENER_P_H
#define IIOEVENTLISTENER_P_H

#include "platform_p.h"
#include "types.h"
#include "iovaluetypes_p.h"

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

#endif // IIOEVENTLISTENER_P_H
