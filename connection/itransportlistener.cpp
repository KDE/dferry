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

#include "itransportlistener.h"

#include "itransport.h"


ITransportListener::ITransportListener()
   : m_readNotificationEnabled(false),
     m_writeNotificationEnabled(false),
     m_transport(0)
{
}

ITransportListener::~ITransportListener()
{
    if (m_transport) {
        m_transport->removeListener(this);
    }
    m_transport = 0;
}

void ITransportListener::setReadNotificationEnabled(bool enable)
{
    if (enable == m_readNotificationEnabled) {
        return;
    }
    m_readNotificationEnabled = enable;
    m_transport->updateReadWriteInterest();
}

bool ITransportListener::readNotificationEnabled() const
{
    return m_readNotificationEnabled;
}

void ITransportListener::setWriteNotificationEnabled(bool enable)
{
    if (enable == m_writeNotificationEnabled) {
        return;
    }
    m_writeNotificationEnabled = enable;
    m_transport->updateReadWriteInterest();
}

bool ITransportListener::writeNotificationEnabled() const
{
    return m_writeNotificationEnabled;
}

void ITransportListener::handleTransportCanRead()
{
}

void ITransportListener::handleTransportCanWrite()
{
}

ITransport *ITransportListener::transport() const
{
    return m_transport;
}
