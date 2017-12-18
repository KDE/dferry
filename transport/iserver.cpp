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

#include "connectaddress.h"
#include "eventdispatcher_p.h"
#include "itransport.h"
#include "ipserver.h"
#ifdef __unix__
#include "localserver.h"
#endif

#include <string>

IServer::IServer()
   : m_newConnectionListener(nullptr),
     m_eventDispatcher(nullptr)
{
}

IServer::~IServer()
{
    for (ITransport *c : m_incomingConnections) {
        delete c;
    }
}

//static
IServer *IServer::create(const ConnectAddress &ca)
{
    if (ca.bus() != ConnectAddress::Bus::PeerToPeer) {
        return nullptr;
    }

    switch (ca.socketType()) {
#ifdef __unix__
    case ConnectAddress::SocketType::Unix:
        return new LocalServer(ca.path());
    case ConnectAddress::SocketType::AbstractUnix:
        return new LocalServer(std::string(1, '\0') + ca.path());
#endif
    case ConnectAddress::SocketType::Ip:
        return new IpServer(ca);
    default:
        return nullptr;
    }
}

ITransport *IServer::takeNextClient()
{
    if (m_incomingConnections.empty()) {
        return nullptr;
    }
    ITransport *ret = m_incomingConnections.front();
    m_incomingConnections.pop_front();
    return ret;
}

void IServer::setNewConnectionListener(ICompletionListener *listener)
{
    m_newConnectionListener = listener;
}

void IServer::setEventDispatcher(EventDispatcher *ed)
{
    if (m_eventDispatcher == ed) {
        return;
    }
    if (m_eventDispatcher) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        ep->removeIoEventListener(this);
    }
    m_eventDispatcher = ed;
    if (m_eventDispatcher) {
        EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
        ep->addIoEventListener(this);
        ep->setReadWriteInterest(this, true, false);
    }
}

EventDispatcher *IServer::eventDispatcher() const
{
    return m_eventDispatcher;
}