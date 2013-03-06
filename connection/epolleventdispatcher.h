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

#ifndef EPOLLEVENTDISPATCHER_H
#define EPOLLEVENTDISPATCHER_H

#include "ieventdispatcher.h"

#include <map>

class EpollEventDispatcher : public IEventDispatcher
{
public:
    EpollEventDispatcher();
    virtual ~EpollEventDispatcher();
    virtual void poll(int timeout = -1);

    FileDescriptor pollDescriptor() const;

protected:
    // reimplemented from IEventDispatcher
    bool addConnection(IConnection *conn);
    bool removeConnection(IConnection *conn);
    void setReadWriteInterest(IConnection *conn, bool read, bool write);

private:
    void notifyRead(int fd);

    FileDescriptor m_epollFd;
};

#endif // EPOLLEVENTDISPATCHER_H
