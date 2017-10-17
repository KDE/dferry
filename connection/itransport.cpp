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

#include "itransport.h"

#include "eventdispatcher.h"
#include "eventdispatcher_p.h"
#include "itransportlistener.h"
#include "ipsocket.h"
#include "connectaddress.h"

#ifdef __unix__
#include "localsocket.h"
#endif

#include <algorithm>
#include <cassert>

using namespace std;

ITransport::ITransport()
   : m_supportsFileDescriptors(false),
     m_eventDispatcher(0),
     m_readNotificationEnabled(false),
     m_writeNotificationEnabled(false)
{
}

ITransport::~ITransport()
{
    vector<ITransportListener *> listenersCopy = m_listeners;
    for (size_t i = listenersCopy.size() - 1; i + 1 > 0; i--) {
        removeListener(listenersCopy[i]); // LIFO (stack) order seems safest...
    }
}

chunk ITransport::readWithFileDescriptors(byte *buffer, uint32 maxSize, vector<int> *)
{
    return read(buffer, maxSize);
}

uint32 ITransport::writeWithFileDescriptors(chunk data, const vector<int> &)
{
    return write(data);
}

void ITransport::addListener(ITransportListener *listener)
{
    if (find(m_listeners.begin(), m_listeners.end(), listener) != m_listeners.end()) {
        return;
    }
    m_listeners.push_back(listener);
    listener->m_transport = this;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void ITransport::removeListener(ITransportListener *listener)
{
    vector<ITransportListener *>::iterator it = find(m_listeners.begin(), m_listeners.end(), listener);
    if (it == m_listeners.end()) {
        return;
    }
    m_listeners.erase(it);
    listener->m_transport = nullptr;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void ITransport::updateReadWriteInterest()
{
    bool readInterest = false;
    bool writeInterest = false;
    for (ITransportListener *listener : m_listeners) {
        if (listener->readNotificationEnabled()) {
            readInterest = true;
        }
        if (listener->writeNotificationEnabled()) {
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

void ITransport::setEventDispatcher(EventDispatcher *ed)
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
        m_readNotificationEnabled = false;
        m_writeNotificationEnabled = false;
        updateReadWriteInterest();
    }
}

EventDispatcher *ITransport::eventDispatcher() const
{
    return m_eventDispatcher;
}

void ITransport::handleCanRead()
{
    for (ITransportListener *listener : m_listeners) {
        if (listener->readNotificationEnabled()) {
            listener->handleTransportCanRead();
            break;
        }
    }
}

void ITransport::handleCanWrite()
{
    for (ITransportListener *listener : m_listeners) {
        if (listener->writeNotificationEnabled()) {
            listener->handleTransportCanWrite();
            break;
        }
    }
}

//static
ITransport *ITransport::create(const ConnectAddress &ci)
{
    switch (ci.socketType()) {
#ifdef __unix__
        case ConnectAddress::SocketType::Unix:
        return new LocalSocket(ci.path());
    case ConnectAddress::SocketType::AbstractUnix:
        return new LocalSocket(string(1, '\0') + ci.path());
#endif
    case ConnectAddress::SocketType::Ip:
        return new IpSocket(ci);
    default:
        assert(false);
        return nullptr;
    }
}
