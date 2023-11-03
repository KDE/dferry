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

#include "ipserver.h"

#include "connectaddress.h"
#include "ipresolver.h"

#include "icompletionlistener.h"
#include "ipsocket.h"

// The "arm Keil MDK Middleware Network Component" has certain quirks that we cover here. For lack
// of a really good way to detect it, we assume that builds with the ARM compiler are for it.
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define KEIL_MDK_NETWORK
#endif

#ifdef __unix__
#include <netinet/in.h>
#include <sys/socket.h>
#ifndef KEIL_MDK_NETWORK
#include <fcntl.h>
#endif
#include <unistd.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif

#include <cassert>
#include <cstring>

#include <iostream>

// TODO implement address family (IPv4 / IPv6) support
IpServer::IpServer(const ConnectAddress &ca)
   : m_listenFd(-1)
{
    assert(ca.type() == ConnectAddress::Type::Tcp);

    const FileDescriptor fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidFileDescriptor(fd)) {
        std::cerr << "IpServer contruction failed A.\n";
        return;
    }
#if !defined(_WIN32) && !defined (KEIL_MDK_NETWORK)
    // don't let forks inherit the file descriptor - just in case
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    IpResolver resolver(ca);
    bool ok = resolver.resultValid();

    ok = ok && bind(fd, resolver.resolved(), resolver.resolvedLength()) == 0;
    ok = ok && (::listen(fd, /* max queued incoming connections */ 64) == 0);

    if (ok) {
        m_listenFd = fd;
    } else {
        std::cerr << "IpServer contruction failed B.\n";
#if defined(_WIN32) || defined (KEIL_MDK_NETWORK)
        closesocket(fd);
#else
        ::close(fd);
#endif
    }
}

IpServer::~IpServer()
{
    close();
}

IO::Status IpServer::handleIoReady(IO::RW rw)
{
    if (rw != IO::RW::Read) {
        assert(false);
        return IO::Status::InternalError;
    }
    FileDescriptor connFd = accept(m_listenFd, nullptr, nullptr);
    if (!isValidFileDescriptor(connFd)) {
        std::cerr << "\nIpServer::notifyRead(): accept() failed.\n\n";
        return IO::Status::RemoteClosed;
    }
#if defined(__unix__) && !defined (KEIL_MDK_NETWORK)
    fcntl(connFd, F_SETFD, FD_CLOEXEC);
#endif
    m_incomingConnections.push_back(new IpSocket(connFd));
    if (m_newConnectionListener) {
        m_newConnectionListener->handleCompletion(this);
    }
    return IO::Status::OK;
}

bool IpServer::isListening() const
{
    return isValidFileDescriptor(m_listenFd);
}

void IpServer::platformClose()
{
    if (isValidFileDescriptor(m_listenFd)) {
#if defined(_WIN32) || defined (KEIL_MDK_NETWORK)
        closesocket(m_listenFd);
#else
        ::close(m_listenFd);
#endif
        m_listenFd = InvalidFileDescriptor;
    }
}

FileDescriptor IpServer::fileDescriptor() const
{
    return m_listenFd;
}
