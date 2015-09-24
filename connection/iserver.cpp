/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "iserver.h"

#include "connectioninfo.h"
#include "eventdispatcher_p.h"
#include "iconnection.h"
#include "ipserver.h"
#include "localserver.h"

#include <string>

IServer::IServer()
   : m_newConnectionClient(nullptr),
     m_eventDispatcher(nullptr)
{
}

IServer::~IServer()
{
    for (IConnection *c : m_incomingConnections) {
        delete c;
    }
}

//static
IServer *IServer::create(const ConnectionInfo &ci)
{
    if (ci.bus() != ConnectionInfo::Bus::PeerToPeer) {
        return nullptr;
    }

    switch (ci.socketType()) {
    case ConnectionInfo::SocketType::Unix:
        return new LocalServer(ci.path());
    case ConnectionInfo::SocketType::AbstractUnix:
        return new LocalServer(std::string(1, '\0') + ci.path());
    case ConnectionInfo::SocketType::Ip:
        return new IpServer(ci);
    default:
        return nullptr;
    }
}

IConnection *IServer::takeNextConnection()
{
    if (m_incomingConnections.empty()) {
        return nullptr;
    }
    IConnection *ret = m_incomingConnections.front();
    m_incomingConnections.pop_front();
    return ret;
}

void IServer::setNewConnectionClient(ICompletionClient *client)
{
    m_newConnectionClient = client;
}

void IServer::setEventDispatcher(EventDispatcher *ed)
{
    if (m_eventDispatcher == ed) {
        return;
    }
    if (m_eventDispatcher) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        ep->removeIoEventClient(this);
    }
    m_eventDispatcher = ed;
    if (m_eventDispatcher) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        ep->addIoEventClient(this);
        ep->setReadWriteInterest(this, true, false);
    }
}

EventDispatcher *IServer::eventDispatcher() const
{
    return m_eventDispatcher;
}

void IServer::notifyRead()
{
}

void IServer::notifyWrite()
{
}
