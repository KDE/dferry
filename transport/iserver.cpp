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
#include <iostream>

#ifdef __unix__
#include <random>
#include "stringtools.h"

static std::string randomDbusSocketName()
{
    std::random_device rd;
    std::mt19937 mt(rd());
    char randomData[16];
    // OK dead code elimination, show us what you can!
    if (sizeof(size_t) >= 8) {
        std::uniform_int_distribution<uint64> dist;
        for (size_t i = 0; i < (sizeof(randomData) / sizeof(uint64)); i++) {
            reinterpret_cast<uint64 *>(randomData)[i] = dist(mt);
        }
    } else {
        std::uniform_int_distribution<uint32> dist;
        for (size_t i = 0; i < (sizeof(randomData) / sizeof(uint32)); i++) {
            reinterpret_cast<uint32 *>(randomData)[i] = dist(mt);
        }
    }
    // Good that std::string knows nothing about valid utf-8 encoding!
    const std::string pseudoString(randomData, sizeof(randomData));
    return std::string("/dbus-") + hexEncode(pseudoString);
}

static std::string xdgRuntimeDir()
{
    return std::string(getenv("XDG_RUNTIME_DIR"));
}
#endif

IServer::IServer()
   : m_newConnectionListener(nullptr)
{
    setIoInterest(uint32(IO::RW::Read));
}

IServer::~IServer()
{
    for (ITransport *c : m_incomingConnections) {
        delete c;
    }
}

//static
IServer *IServer::create(const ConnectAddress &listenAddr, ConnectAddress *concreteAddr)
{
    if (listenAddr.role() != ConnectAddress::Role::PeerServer) {
        return nullptr;
    }

#ifdef __unix__
    bool isLocalSocket = true;
    bool isAbstract = false;
    std::string unixSocketPath;
#endif

    switch (listenAddr.type()) {
#ifdef __unix__
    case ConnectAddress::Type::UnixPath:
        unixSocketPath = listenAddr.path();
        break;
    case ConnectAddress::Type::UnixDir:
        unixSocketPath = listenAddr.path() + randomDbusSocketName();
        break;
    case ConnectAddress::Type::RuntimeDir:
        unixSocketPath = xdgRuntimeDir() + randomDbusSocketName();
        break;
    case ConnectAddress::Type::TmpDir:
        unixSocketPath = listenAddr.path() + randomDbusSocketName();
#ifdef __linux__
        isAbstract = true;
#endif
        break;
#ifdef __linux__
    case ConnectAddress::Type::AbstractUnixPath:
        unixSocketPath = listenAddr.path();
        isAbstract = true;
        break;
#endif
#endif
    case ConnectAddress::Type::Tcp:
    case ConnectAddress::Type::Tcp4:
    case ConnectAddress::Type::Tcp6:
#ifdef __unix__
        isLocalSocket = false;
#endif
        break;
    default:
        return nullptr;
    }

    *concreteAddr = listenAddr;

#ifdef __unix__
    if (isLocalSocket) {
        concreteAddr->setType(isAbstract ? ConnectAddress::Type::AbstractUnixPath
                                         : ConnectAddress::Type::UnixPath);
        concreteAddr->setPath(unixSocketPath);
        if (isAbstract) {
            unixSocketPath.insert(0, 1, '\0');
        }
        return new LocalServer(unixSocketPath);
    } else
#endif
        return new IpServer(listenAddr);
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

void IServer::close()
{
    if (!isListening()) {
        return;
    }
    if (ioEventSource()) {
        ioEventSource()->removeIoListener(this);
    }
    platformClose();
}
