// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "itransportlistener_p.h"

#include "itransport_p.h"

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

#endif // RUNNING_DOXYGEN
