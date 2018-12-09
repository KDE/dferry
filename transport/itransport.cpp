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

ITransport::ITransport()
{
}

ITransport::~ITransport()
{
    setReadListener(nullptr);
    setWriteListener(nullptr);
}

IO::Result ITransport::readWithFileDescriptors(byte *buffer, uint32 maxSize, std::vector<int> *)
{
    return read(buffer, maxSize);
}

IO::Result ITransport::writeWithFileDescriptors(chunk data, const std::vector<int> &)
{
    return write(data);
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
        // TODO call some common close, cleanup & report error method
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
