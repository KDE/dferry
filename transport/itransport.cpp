// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "itransport_p.h"

#include "eventdispatcher.h"
#include "eventdispatcher_p.h"
#include "itransportlistener_p.h"
#include "ipsocket_p.h"
#include "connectaddress.h"

#ifdef __unix__
#include "localsocket_p.h"
#endif

#include <algorithm>
#include <cassert>

ITransport::ITransport()
{
}

ITransport::~ITransport()
{
    setReadListener(nullptr);
    setWriteListener(nullptr);
}

void ITransport::setReadListener(ITransportListener *listener)
{
    if (m_readListener != listener) {
        if (m_readListener) {
            m_readListener->m_readTransport = nullptr;
        }
        if (listener) {
            if (listener->m_readTransport) {
                listener->m_readTransport->setReadListener(nullptr);
            }
            assert(!listener->m_readTransport);
            listener->m_readTransport = this;
        }
        m_readListener = listener;
    }
    updateTransportIoInterest();
}

void ITransport::setWriteListener(ITransportListener *listener)
{
    if (m_writeListener != listener) {
        if (m_writeListener) {
            m_writeListener->m_writeTransport = nullptr;
        }
        if (listener) {
            if (listener->m_writeTransport) {
                listener->m_writeTransport->setWriteListener(nullptr);
            }
            assert(!listener->m_writeTransport);
            listener->m_writeTransport = this;
        }
        m_writeListener = listener;
    }
    updateTransportIoInterest();
}

void ITransport::updateTransportIoInterest()
{
    setIoInterest((m_readListener ? uint32(IO::RW::Read) : 0) |
                  (m_writeListener ? uint32(IO::RW::Write) : 0));
}

void ITransport::close()
{
    if (!isOpen()) {
        return;
    }
    if (ioEventSource()) {
        ioEventSource()->removeIoListener(this);
    }
    platformClose();
}

IO::Status ITransport::handleIoReady(IO::RW rw)
{
    IO::Status ret = IO::Status::OK;
    assert(uint32(rw) & ioInterest()); // only get notified about events we requested
    if (rw == IO::RW::Read && m_readListener) {
        ret = m_readListener->handleTransportCanRead();
    } else if (rw == IO::RW::Write && m_writeListener) {
        ret = m_writeListener->handleTransportCanWrite();
    } else {
        assert(false);
    }
    if (ret != IO::Status::OK) {
        // ### anything else to do?
        close();
    }
    return ret;
}

//static
ITransport *ITransport::create(const ConnectAddress &ci)
{
    switch (ci.type()) {
#ifdef __unix__
        case ConnectAddress::Type::UnixPath:
        return new LocalSocket(ci.path());
    case ConnectAddress::Type::AbstractUnixPath: // TODO this is Linux only, reflect it in code
        return new LocalSocket(std::string(1, '\0') + ci.path());
#endif
    case ConnectAddress::Type::Tcp:
    case ConnectAddress::Type::Tcp4:
    case ConnectAddress::Type::Tcp6:
        return new IpSocket(ci);
    default:
        assert(false);
        return nullptr;
    }
}

#endif // RUNNING_DOXYGEN
