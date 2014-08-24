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
#include "iconnectionclient.h"
#include "localsocket.h"
#include "peeraddress.h"

#include <algorithm>
#include <cassert>
#include <iostream>

using namespace std;

IConnection::IConnection()
   : m_eventDispatcher(0),
     m_isReadNotificationEnabled(false),
     m_isWriteNotificationEnabled(false)
{
}

IConnection::~IConnection()
{
    setEventDispatcher(0);
    vector<IConnectionClient *> clientsCopy = m_clients;
    for (int i = clientsCopy.size() - 1; i >= 0; i--) {
        removeClient(clientsCopy[i]); // LIFO (stack) order seems safest...
    }
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
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isReadNotificationEnabled()) {
            readInterest = true;
        }
        if (m_clients[i]->isWriteNotificationEnabled()) {
            writeInterest = true;
        }
    }
    if (readInterest != m_isReadNotificationEnabled || writeInterest != m_isWriteNotificationEnabled) {
        m_isReadNotificationEnabled = readInterest;
        m_isWriteNotificationEnabled = writeInterest;
        m_eventDispatcher->setReadWriteInterest(this, m_isReadNotificationEnabled,
                                                m_isWriteNotificationEnabled);
    }
}

void IConnection::setEventDispatcher(EventDispatcher *ed)
{
    if (m_eventDispatcher == ed) {
        return;
    }
    if (m_eventDispatcher) {
        m_eventDispatcher->removeConnection(this);
    }
    m_eventDispatcher = ed;
    if (m_eventDispatcher) {
        m_eventDispatcher->addConnection(this);
        m_isReadNotificationEnabled = false;
        m_isWriteNotificationEnabled = false;
        updateReadWriteInterest();
    }
}

EventDispatcher *IConnection::eventDispatcher() const
{
    return m_eventDispatcher;
}

void IConnection::notifyRead()
{
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isReadNotificationEnabled()) {
            m_clients[i]->notifyConnectionReadyRead();
            break;
        }
    }
}

void IConnection::notifyWrite()
{
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isWriteNotificationEnabled()) {
            m_clients[i]->notifyConnectionReadyWrite();
            break;
        }
    }
}

//static
IConnection *IConnection::create(const PeerAddress &address)
{
    switch (address.socketType()) {
    case PeerAddress::UnixSocket:
        return new LocalSocket(address.path());
    case PeerAddress::AbstractUnixSocket:
        return new LocalSocket(string(1, '\0') + address.path());
    default:
        assert(false);
        return 0;
    }
}
