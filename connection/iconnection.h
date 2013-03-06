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

#include "platform.h"
#include "types.h"

#include <vector>

class IConnectionClient;
class IEventDispatcher;

class IConnection
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

    virtual int availableBytesForReading() = 0;
    virtual array read(byte *buffer, int maxSize) = 0;
    virtual int write(array data) = 0;
    virtual void close() = 0;

    virtual bool isOpen() = 0;
    virtual FileDescriptor fileDescriptor() const = 0;

    virtual void setEventDispatcher(IEventDispatcher *ed);
    virtual IEventDispatcher *eventDispatcher() const;

protected:
    friend class IEventDispatcher;
    // called from the event dispatcher. virtual because at least LocalSocket requires extra logic.
    virtual void notifyRead();
    virtual void notifyWrite();

private:
    friend class IConnectionClient;
    void updateReadWriteInterest(); // called internally and from IConnectionClient

    IEventDispatcher *m_eventDispatcher;
    std::vector<IConnectionClient *> m_clients;
    bool m_isReadNotificationEnabled;
    bool m_isWriteNotificationEnabled;
};
