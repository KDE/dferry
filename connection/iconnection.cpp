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

#include "iconnection.h"

#include "eventdispatcher.h"
#include "eventdispatcher_p.h"
#include "iconnectionclient.h"
#include "ipsocket.h"
#include "connectioninfo.h"

#ifdef __unix__
#include "localsocket.h"
#endif

#include <algorithm>
#include <cassert>

using namespace std;

IConnection::IConnection()
   : m_supportsFileDescriptors(false),
     m_eventDispatcher(0),
     m_readNotificationEnabled(false),
     m_writeNotificationEnabled(false)
{
}

IConnection::~IConnection()
{
    vector<IConnectionClient *> clientsCopy = m_clients;
    for (size_t i = clientsCopy.size() - 1; i + 1 > 0; i--) {
        removeClient(clientsCopy[i]); // LIFO (stack) order seems safest...
    }
}

chunk IConnection::readWithFileDescriptors(byte *buffer, uint32 maxSize, vector<int> *)
{
    return read(buffer, maxSize);
}

uint32 IConnection::writeWithFileDescriptors(chunk data, const vector<int> &)
{
    return write(data);
}

void IConnection::addClient(IConnectionClient *client)
{
    if (find(m_clients.begin(), m_clients.end(), client) != m_clients.end()) {
        return;
    }
    m_clients.push_back(client);
    client->m_connection = this;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void IConnection::removeClient(IConnectionClient *client)
{
    vector<IConnectionClient *>::iterator it = find(m_clients.begin(), m_clients.end(), client);
    if (it == m_clients.end()) {
        return;
    }
    m_clients.erase(it);
    client->m_connection = 0;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void IConnection::updateReadWriteInterest()
{
    bool readInterest = false;
    bool writeInterest = false;
    for (IConnectionClient *client : m_clients) {
        if (client->readNotificationEnabled()) {
            readInterest = true;
        }
        if (client->writeNotificationEnabled()) {
            writeInterest = true;
        }
    }
    if (readInterest != m_readNotificationEnabled || writeInterest != m_writeNotificationEnabled) {
        m_readNotificationEnabled = readInterest;
        m_writeNotificationEnabled = writeInterest;
        if (m_eventDispatcher) {
            EventDispatcherPrivate *const ep = EventDispatcherPrivate::get(m_eventDispatcher);
            ep->setReadWriteInterest(this, m_readNotificationEnabled, m_writeNotificationEnabled);
        }
    }
}

void IConnection::setEventDispatcher(EventDispatcher *ed)
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
        m_readNotificationEnabled = false;
        m_writeNotificationEnabled = false;
        updateReadWriteInterest();
    }
}

EventDispatcher *IConnection::eventDispatcher() const
{
    return m_eventDispatcher;
}

void IConnection::handleCanRead()
{
    for (IConnectionClient *client : m_clients) {
        if (client->readNotificationEnabled()) {
            client->handleConnectionCanRead();
            break;
        }
    }
}

void IConnection::handleCanWrite()
{
    for (IConnectionClient *client : m_clients) {
        if (client->writeNotificationEnabled()) {
            client->handleConnectionCanWrite();
            break;
        }
    }
}

//static
IConnection *IConnection::create(const ConnectionInfo &ci)
{
    switch (ci.socketType()) {
#ifdef __unix__
        case ConnectionInfo::SocketType::Unix:
        return new LocalSocket(ci.path());
    case ConnectionInfo::SocketType::AbstractUnix:
        return new LocalSocket(string(1, '\0') + ci.path());
#endif
    case ConnectionInfo::SocketType::Ip:
        return new IpSocket(ci);
    default:
        assert(false);
        return nullptr;
    }
}
