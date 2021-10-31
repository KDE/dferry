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

#include "connectaddress.h"

#ifdef __unix__
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#endif
#ifdef _WIN32
#include <winsock2.h>
typedef SSIZE_T ssize_t;
#endif

#include <errno.h>

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>

#include <iostream>

static bool setNonBlocking(int fd)
{
#ifdef _WIN32
    unsigned long value = 1; // 0 blocking, != 0 non-blocking
    if (ioctlsocket(fd, FIONBIO, &value) != NO_ERROR) {
        return false;
    }
#else
    // don't let forks inherit the file descriptor - that can cause confusion...
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    // To be able to use the same send() and recv() calls as Windows, also set the non-blocking
    // property on the socket descriptor here instead of passing MSG_DONTWAIT to send() and recv().
    const int oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags == -1) {
        return false;
    }
    fcntl(fd, F_SETFL, oldFlags | O_NONBLOCK);
#endif
    return true;
}

static int sendFlags()
{
#ifdef _WIN32
    return 0;
#else
    return MSG_NOSIGNAL;
#endif
}

static void closeSocket(int fd)
{
#ifdef _WIN32
    closesocket(fd);
#else
    ::close(fd);
#endif
}

// TODO implement address family (IPv4 / IPv6) support
IpSocket::IpSocket(const ConnectAddress &ca)
   : m_fd(-1)
{
    assert(ca.type() == ConnectAddress::Type::Tcp);
#ifdef _WIN32
    WSAData wsadata;
    // IPv6 requires Winsock v2.0 or better (but we're not using IPv6 - yet!)
    if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0) {
        std::cerr << "IpSocket contruction failed A.\n";
        return;
    }
#endif
    const FileDescriptor fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidFileDescriptor(fd)) {
        std::cerr << "IpSocket contruction failed B.\n";
        return;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(ca.port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    // only make it non-blocking after connect() because Winsock returns
    // WSAEWOULDBLOCK when connecting a non-blocking socket
    ok = ok && setNonBlocking(fd);

    if (ok) {
        m_fd = fd;
    } else {
        closeSocket(fd);
    }
}

IpSocket::IpSocket(FileDescriptor fd)
   : m_fd(fd)
{
    if (!setNonBlocking(m_fd)) {
        closeSocket(fd);
        m_fd = -1;
    }
}

IpSocket::~IpSocket()
{
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

void IpSocket::platformClose()
{
    if (isValidFileDescriptor(m_fd)) {
        closeSocket(m_fd);
        m_fd = InvalidFileDescriptor;
    }
}

IO::Result IpSocket::write(chunk a)
{
    IO::Result ret;
    if (!isValidFileDescriptor(m_fd)) {
        std::cerr << "\nIpSocket::write() failed A.\n\n";
        ret.status = IO::Status::InternalError;
        return ret;
    }

    const uint32 initialLength = a.length;

    while (a.length > 0) {
        ssize_t nbytes = send(m_fd, reinterpret_cast<char *>(a.ptr), a.length, sendFlags());
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see EAGAIN comment in LocalSocket::read()
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            ret.status = IO::Status::InternalError;
            return ret;
        } else if (nbytes == 0) {
            break;
        }

        a.ptr += nbytes;
        a.length -= uint32(nbytes);
    }

    ret.length = initialLength - a.length;
    return ret;
}

uint32 IpSocket::availableBytesForReading()
{
#ifdef _WIN32
    u_long available = 0;
    if (ioctlsocket(m_fd, FIONREAD, &available) != NO_ERROR) {
#else
    uint32 available = 0;
    if (ioctl(m_fd, FIONREAD, &available) < 0) {
#endif
        available = 0;
    }
    return uint32(available);
}

IO::Result IpSocket::read(byte *buffer, uint32 maxSize)
{
    IO::Result ret;
    if (maxSize <= 0) {
        std::cerr << "\nIpSocket::read() failed A.\n\n";
        ret.status = IO::Status::InternalError;
        return ret;
    }

    while (maxSize > 0) {
        ssize_t nbytes = recv(m_fd, reinterpret_cast<char *>(buffer), maxSize, 0);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see comment in LocalSocket for rationale of EAGAIN behavior
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            ret.status = IO::Status::RemoteClosed;
            break;
        } else if (nbytes == 0) {
            // orderly shutdown
            close();
            ret.status = IO::Status::RemoteClosed;
            break;
        }
        ret.length += uint32(nbytes);
        buffer += nbytes;
        maxSize -= uint32(nbytes);
    }

    return ret;
}

bool IpSocket::isOpen()
{
    return isValidFileDescriptor(m_fd);
}

FileDescriptor IpSocket::fileDescriptor() const
{
    return m_fd;
}
