/*
   Copyright (C) 2015 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "ipsocket.h"

#include "connectioninfo.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>

// HACK, put this somewhere else (get the value from original d-bus? or is it infinite?)
static const int maxFds = 12;

using namespace std;

IpSocket::IpSocket(const ConnectionInfo &ci)
   : m_fd(-1)
{
    assert(ci.socketType() == ConnectionInfo::SocketType::Ip);
#ifdef WINDOWS
    WSAData wsadata;
    // IPv6 requires Winsock v2.0 or better (but we're not using IPv6 - yet!)
    if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0) {
        return;
    }
#endif
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    // don't let forks inherit the file descriptor - that can cause confusion...
    fcntl(fd, F_SETFD, FD_CLOEXEC);

#ifdef WINDOWS
    unsigned long value = 1; // 0 blocking, != 0 non-blocking
    if (ioctlsocket(fd, FIONBIO, &value) != NO_ERROR) {
        // something along the lines of... WS_ERROR_DEBUG(WSAGetLastError());
        closesocket(fd);
        return;
    }
#else
    // To be able to use the same send() and recv() calls as Windows, also set the non-blocking
    // property on the socket descriptor here instead of passing MSG_DONTWAIT to send() and recv().
    const int oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags == -1) {
        ::close(fd);
        return;
    }
    fcntl(fd, F_SETFL, oldFlags & O_NONBLOCK);
#endif

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ci.port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;

    if (ok) {
        m_fd = fd;
    } else {
#ifdef WINDOWS
        closesocket(fd);
#else
        ::close(fd);
#endif
    }
}

IpSocket::IpSocket(int fd)
   : m_fd(fd)
{
}

IpSocket::~IpSocket()
{
    close();
#ifdef WINDOWS
    WSACleanup();
#endif
}

void IpSocket::close()
{
    setEventDispatcher(nullptr);
    if (m_fd >= 0) {
#ifdef WINDOWS
        closesocket(m_fd);
#else
        ::close(m_fd);
#endif
    }
    m_fd = -1;
}

uint32 IpSocket::write(chunk a)
{
    if (m_fd < 0) {
        return 0; // TODO -1?
    }

    const uint32 initialLength = a.length;

    while (a.length > 0) {
        ssize_t nbytes = send(m_fd, reinterpret_cast<char *>(a.ptr), a.length, 0);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see EAGAIN comment in LocalSocket::read()
            if (errno == EAGAIN) {
                break;
            }
            close();
            return false;
        }

        a.ptr += nbytes;
        a.length -= uint32(nbytes);
    }

    return initialLength - a.length;
}

uint32 IpSocket::availableBytesForReading()
{
    uint32 available = 0;
    if (ioctl(m_fd, FIONREAD, &available) < 0) {
        available = 0;
    }
    return available;
}

chunk IpSocket::read(byte *buffer, uint32 maxSize)
{
    chunk ret;
    if (maxSize <= 0) {
        return ret;
    }

    const uint32 bufSize = maxSize;

    while (maxSize > 0) {
        ssize_t nbytes = recv(m_fd, reinterpret_cast<char *>(buffer), maxSize, 0);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see comment in LocalSocket for rationale of EAGAIN behavior
            if (errno == EAGAIN) {
                break;
            }
            close();
            return ret;
        }
        ret.length += uint32(nbytes);
        buffer += nbytes;
        maxSize -= uint32(nbytes);
    }

    return ret;
}

bool IpSocket::isOpen()
{
    return m_fd != -1;
}

int IpSocket::fileDescriptor() const
{
    return m_fd;
}

void IpSocket::notifyRead()
{
    if (availableBytesForReading()) {
        IConnection::notifyRead();
    } else {
        // This should really only happen in error cases! ### TODO test?
        close();
    }
}
