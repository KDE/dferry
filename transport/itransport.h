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

class ITransport : public IioEventListener
{
public:
    // An ITransport subclass must have a file descriptor after construction and it must not change
    // except to the invalid file descriptor when disconnected.
    ITransport(); // TODO event dispatcher as constructor argument?
    ~ITransport() override;

    // usually, the maximum sensible number of listeners is two: one for reading and one for writing.
    // avoiding (independent) readers and writers blocking each other is good for IO efficiency.
    void addListener(ITransportListener *listener);
    void removeListener(ITransportListener *listener);

    virtual uint32 availableBytesForReading() = 0;
    virtual chunk read(byte *buffer, uint32 maxSize) = 0;
    virtual chunk readWithFileDescriptors(byte *buffer, uint32 maxSize, std::vector<int> *fileDescriptors);
    virtual uint32 write(chunk data) = 0;
    virtual uint32 writeWithFileDescriptors(chunk data, const std::vector<int> &fileDescriptors);

    virtual void close() = 0;
    virtual bool isOpen() = 0;

    bool supportsPassingFileDescriptors() const { return m_supportsFileDescriptors; }

    void setEventDispatcher(EventDispatcher *ed) override;
    EventDispatcher *eventDispatcher() const override;

    // factory method - creates a suitable subclass to connect to address
    static ITransport *create(const ConnectAddress &connectAddress);

protected:
    friend class EventDispatcher;
    // IioEventListener
    void handleCanRead() override;
    void handleCanWrite() override;

    bool m_supportsFileDescriptors;

private:
    friend class ITransportListener;
    friend class SelectEventPoller;
    void updateReadWriteInterest(); // called internally and from ITransportListener

    EventDispatcher *m_eventDispatcher;
    std::vector<ITransportListener *> m_listeners;
    bool m_readNotificationEnabled;
    bool m_writeNotificationEnabled;
};

#endif // ITRANSPORT_H
