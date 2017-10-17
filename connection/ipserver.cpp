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

#include "connectioninfo.h"

#include "icompletionclient.h"
#include "ipsocket.h"

#ifdef __unix__
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
#endif

#include <cassert>
#include <cstring>

#include <iostream>

IpServer::IpServer(const ConnectionInfo &ci)
   : m_listenFd(-1)
{
    assert(ci.socketType() == ConnectionInfo::SocketType::Ip);

    const FileDescriptor fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidFileDescriptor(fd)) {
        std::cerr << "IpServer contruction failed A.\n";
        return;
    }
#ifdef __unix__
    // don't let forks inherit the file descriptor - just in case
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ci.port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    ok = ok && (::listen(fd, /* max queued incoming connections */ 64) == 0);

    if (ok) {
        m_listenFd = fd;
    } else {
        std::cerr << "IpServer contruction failed B.\n";
#ifdef _WIN32
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

void IpServer::handleCanRead()
{
    setEventDispatcher(nullptr);
    FileDescriptor connFd = accept(m_listenFd, nullptr, nullptr);
    if (!isValidFileDescriptor(connFd)) {
        std::cerr << "\nIpServer::notifyRead(): accept() failed.\n\n";
        return;
    }
#ifdef __unix__
    fcntl(connFd, F_SETFD, FD_CLOEXEC);
#endif
    m_incomingConnections.push_back(new IpSocket(connFd));
    if (m_newConnectionClient) {
        m_newConnectionClient->handleCompletion(this);
    }
}

void IpServer::handleCanWrite()
{
    // We never registered this to be called, so...
    assert(false);
}

bool IpServer::isListening() const
{
    return isValidFileDescriptor(m_listenFd);
}

void IpServer::close()
{
    if (isValidFileDescriptor(m_listenFd)) {
#ifdef _WIN32
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
