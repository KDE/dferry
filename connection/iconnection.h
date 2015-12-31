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

#ifndef ICONNECTION_H
#define ICONNECTION_H

#include "iioeventclient.h"
#include "platform.h"
#include "types.h"

#include <vector>

class ConnectionInfo;
class EventDispatcher;
class IConnectionClient;
class SelectEventPoller;

class IConnection : public IioEventClient
{
public:
    // An IConnection subclass must have a file descriptor after construction and it must not change
    // except to the invalid file descriptor when disconnected.
    IConnection(); // TODO event dispatcher as constructor argument?
    virtual ~IConnection();

    // usually, the maximum sensible number of clients is two: one for reading and one for writing.
    // avoiding (independent) readers and writers blocking each other is good for IO efficiency.
    void addClient(IConnectionClient *client);
    void removeClient(IConnectionClient *client);

    virtual uint32 availableBytesForReading() = 0;
    virtual chunk read(byte *buffer, uint32 maxSize) = 0;
    virtual uint32 write(chunk data) = 0;
    virtual void close() = 0;

    virtual bool isOpen() = 0;

    void setEventDispatcher(EventDispatcher *ed) override;
    EventDispatcher *eventDispatcher() const override;

    // factory method - creates a suitable subclass to connect to address
    static IConnection *create(const ConnectionInfo &connectionInfo);

protected:
    friend class EventDispatcher;
    // IioEventClient
    void notifyRead() override;
    void notifyWrite() override;

private:
    friend class IConnectionClient;
    friend class SelectEventPoller;
    void updateReadWriteInterest(); // called internally and from IConnectionClient

    EventDispatcher *m_eventDispatcher;
    std::vector<IConnectionClient *> m_clients;
    bool m_readNotificationEnabled;
    bool m_writeNotificationEnabled;
};

#endif // ICONNECTION_H
