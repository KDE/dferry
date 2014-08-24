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

#include "iconnectionclient.h"

#include "iconnection.h"
#include "ieventdispatcher.h"

IConnectionClient::IConnectionClient()
   : m_isReadNotificationEnabled(false),
     m_isWriteNotificationEnabled(false),
     m_connection(0)
{
}

IConnectionClient::~IConnectionClient()
{
    if (m_connection) {
        m_connection->removeClient(this);
    }
    m_connection = 0;
}

void IConnectionClient::setIsReadNotificationEnabled(bool enable)
{
    if (enable == m_isReadNotificationEnabled) {
        return;
    }
    m_isReadNotificationEnabled = enable;
    m_connection->updateReadWriteInterest();
}

bool IConnectionClient::isReadNotificationEnabled() const
{
    return m_isReadNotificationEnabled;
}

void IConnectionClient::setIsWriteNotificationEnabled(bool enable)
{
    if (enable == m_isWriteNotificationEnabled) {
        return;
    }
    m_isWriteNotificationEnabled = enable;
    m_connection->updateReadWriteInterest();
}

bool IConnectionClient::isWriteNotificationEnabled() const
{
    return m_isWriteNotificationEnabled;
}

void IConnectionClient::notifyConnectionReadyRead()
{
}

void IConnectionClient::notifyConnectionReadyWrite()
{
}

IConnection *IConnectionClient::connection() const
{
    return m_connection;
}
