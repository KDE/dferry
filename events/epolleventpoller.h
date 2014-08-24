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

#ifndef EPOLLEVENTPOLLER_H
#define EPOLLEVENTPOLLER_H

#include "ieventpoller.h"

#include <map>

class EpollEventPoller : public IEventPoller
{
public:
    EpollEventPoller(EventDispatcher *dispatcher);
    virtual ~EpollEventPoller() override;
    virtual bool poll(int timeout) override;
    virtual void interrupt() override;

    // TODO figure out how to handle plugging into other event loops in the general case;
    //      there seems to be some single-fd mechanism available on most platforms and where
    //      there isn't, a list of descriptors (propagate only changes?) should work
    FileDescriptor pollDescriptor() const;

    // reimplemented from IEventPoller
    void addConnection(IConnection *conn) override;
    void removeConnection(IConnection *conn) override;
    void setReadWriteInterest(IConnection *conn, bool read, bool write) override;

private:
    void notifyRead(int fd);

    int m_interruptPipe[2];
    FileDescriptor m_epollFd;
};

#endif // EPOLLEVENTPOLLER_H
