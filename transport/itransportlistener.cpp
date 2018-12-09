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

#include <cassert>

ITransportListener::ITransportListener()
{
}

ITransportListener::~ITransportListener()
{
    if (m_readTransport) {
        m_readTransport->setReadListener(nullptr);
    }
    assert(!m_readTransport);
    if (m_writeTransport) {
        m_writeTransport->setWriteListener(nullptr);
    }
    assert(!m_writeTransport);
}

ITransport *ITransportListener::readTransport() const
{
    return m_readTransport;
}

ITransport *ITransportListener::writeTransport() const
{
    return m_writeTransport;
}

IO::Status ITransportListener::handleTransportCanRead()
{
    return IO::Status::OK;
}

IO::Status ITransportListener::handleTransportCanWrite()
{
    return IO::Status::OK;
}
