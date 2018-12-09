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

#ifndef ITRANSPORT_H
#define ITRANSPORT_H

#include "iioeventlistener.h"
#include "platform.h"
#include "types.h"

#include <vector>

class ConnectAddress;
class EventDispatcher;
class ITransportListener;
class SelectEventPoller;

class ITransport : public IIoEventListener
{
public:
    // An ITransport subclass must have a file descriptor after construction and it must not change
    // except to the invalid file descriptor when disconnected.
    ITransport(); // TODO event dispatcher as constructor argument?
    ~ITransport() override;

    // This listener interface is different from IIoEventSource / IIoEventListener because that one is
    // one source, several file descriptors, one file descriptor to one listener
    // this is one file descriptor, two channels, one channel (read or write) to one listener.
    void setReadListener(ITransportListener *listener);
    void setWriteListener(ITransportListener *listener);

    virtual uint32 availableBytesForReading() = 0;
    virtual IO::Result read(byte *buffer, uint32 maxSize) = 0;
    virtual IO::Result readWithFileDescriptors(byte *buffer, uint32 maxSize,
                                               std::vector<int> *fileDescriptors);
    virtual IO::Result write(chunk data) = 0;
    virtual IO::Result writeWithFileDescriptors(chunk data, const std::vector<int> &fileDescriptors);

    void close();
    virtual bool isOpen() = 0;

    bool supportsPassingFileDescriptors() const { return m_supportsFileDescriptors; }

    IO::Status handleIoReady(IO::RW rw) override;

    // factory method - creates a suitable subclass to connect to address
    static ITransport *create(const ConnectAddress &connectAddress);

protected:
    virtual void platformClose() = 0;
    bool m_supportsFileDescriptors = false;

private:
    void updateTransportIoInterest(); // "Transport" in name to avoid confusion with IIoEventSource
    friend class ITransportListener;
    friend class SelectEventPoller;

    ITransportListener *m_readListener = nullptr;
    ITransportListener *m_writeListener = nullptr;
};

#endif // ITRANSPORT_H
