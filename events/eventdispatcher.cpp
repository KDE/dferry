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

#include "eventdispatcher.h"

#include "epolleventpoller.h"
#include "iconnection.h"
#include "ieventpoller.h"

#include <cstdio>

#define EVENTDISPATCHER_DEBUG

using namespace std;

EventDispatcher::EventDispatcher()
{
    // TODO other backend on other platforms
    m_poller = new EpollEventPoller(this);
}

EventDispatcher::~EventDispatcher()
{
    map<FileDescriptor, IConnection*>::iterator it = m_connections.begin();
    for ( ; it != m_connections.end(); ++it ) {
        it->second->setEventDispatcher(0);
    }
    delete m_poller;
}

bool EventDispatcher::poll(int timeout)
{
    return m_poller->poll(timeout);
}

void EventDispatcher::interrupt()
{
    m_poller->interrupt();
}

bool EventDispatcher::addConnection(IConnection *conn)
{
    pair<map<FileDescriptor, IConnection*>::iterator, bool> insertResult;
    insertResult = m_connections.insert(make_pair(conn->fileDescriptor(), conn));
    const bool ret = insertResult.second;
    if (ret) {
        m_poller->addConnection(conn);
    }
    return ret;
}

bool EventDispatcher::removeConnection(IConnection *conn)
{
    const bool ret = m_connections.erase(conn->fileDescriptor());
    if (ret) {
        m_poller->removeConnection(conn);
    }
    return ret;
}

void EventDispatcher::setReadWriteInterest(IConnection *conn, bool read, bool write)
{
    m_poller->setReadWriteInterest(conn, read, write);
}

void EventDispatcher::notifyConnectionForReading(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

void EventDispatcher::notifyConnectionForWriting(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyWrite();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("EventDispatcher::notifyWrite(): unhandled file descriptor %d.\n", fd);
#endif
    }
}
