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

#include "ieventdispatcher.h"

#include "iconnection.h"

#include <cstdio>

#define IEVENTDISPATCHER_DEBUG

using namespace std;

IEventDispatcher::~IEventDispatcher()
{
    map<FileDescriptor, IConnection*>::iterator it = m_connections.begin();
    for ( ; it != m_connections.end(); it = m_connections.begin() ) {
        it->second->setEventDispatcher(0);
    }
}

bool IEventDispatcher::addConnection(IConnection *conn)
{
    pair<map<FileDescriptor, IConnection*>::iterator, bool> insertResult;
    insertResult = m_connections.insert(make_pair(conn->fileDescriptor(), conn));
    return insertResult.second;
}

bool IEventDispatcher::removeConnection(IConnection *conn)
{
    return m_connections.erase(conn->fileDescriptor());
}

void IEventDispatcher::notifyConnectionForReading(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("IEventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

void IEventDispatcher::notifyConnectionForWriting(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyWrite();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("IEventDispatcher::notifyWrite(): unhandled file descriptor %d.\n", fd);
#endif
    }
}
