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

#include "epolleventdispatcher.h"

#include "iconnection.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>

EpollEventDispatcher::EpollEventDispatcher()
 : m_epollFd(epoll_create(10))
{
}

EpollEventDispatcher::~EpollEventDispatcher()
{
    close(m_epollFd);
}

void EpollEventDispatcher::poll(int timeout)
{
    static const int maxEvPerPoll = 8;
    struct epoll_event results[maxEvPerPoll];
    int nresults = epoll_wait(m_epollFd, results, maxEvPerPoll, timeout);
    if (nresults < 0) {
        // error
        return;
    }

    for (int i = 0; i < nresults; i++) {
        struct epoll_event *evt = results + i;
        if (evt->events & EPOLLIN) {
            notifyConnectionForReading(evt->data.fd);
        }
        if (evt->events & EPOLLOUT) {
            notifyConnectionForWriting(evt->data.fd);
        }
    }
}

FileDescriptor EpollEventDispatcher::pollDescriptor() const
{
    return m_epollFd;
}

bool EpollEventDispatcher::addConnection(IConnection *connection)
{
    if (!IEventDispatcher::addConnection(connection)) {
        return false;
    }
    struct epoll_event epevt;
    epevt.events = 0;
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = connection->fileDescriptor();
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, connection->fileDescriptor(), &epevt);
    return true;
}

bool EpollEventDispatcher::removeConnection(IConnection *connection)
{
    if (!IEventDispatcher::removeConnection(connection)) {
        return false;
    }
    const int connFd = connection->fileDescriptor();
    // Connection should call us *before* resetting its fd on failure
    assert(connFd >= 0);
    struct epoll_event epevt; // required in Linux < 2.6.9 even though it's ignored
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, connFd, &epevt);
    return true;
}

void EpollEventDispatcher::setReadWriteInterest(IConnection *conn, bool readEnabled, bool writeEnabled)
{
    FileDescriptor fd = conn->fileDescriptor();
    if (!fd) {
        return;
    }
    struct epoll_event epevt;
    epevt.events = (readEnabled ? EPOLLIN : 0) | (writeEnabled ? EPOLLOUT : 0);
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = fd;
    epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &epevt);
}
