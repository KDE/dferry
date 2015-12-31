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

// HACK, put this somewhere else (get the value from original d-bus? or is it infinite?)
static const int maxFds = 12;

using namespace std;

IpSocket::IpSocket(const ConnectionInfo &ci)
   : m_fd(-1)
{
    assert(ci.socketType() == ConnectionInfo::SocketType::Ip);
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
    addr.sin_port = htons(ci.port());
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    bool ok = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;

    // only make it non-blocking after connect() because Winsock returns
    // WSAEWOULDBLOCK when connecting a non-blocking socket
#ifdef _WIN32
    unsigned long value = 1; // 0 blocking, != 0 non-blocking
    if (ioctlsocket(fd, FIONBIO, &value) != NO_ERROR) {
        // something along the lines of... WS_ERROR_DEBUG(WSAGetLastError());
        std::cerr << "IpSocket contruction failed C.\n";
        closesocket(fd);
        return;
    }
#else
    // don't let forks inherit the file descriptor - that can cause confusion...
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    // To be able to use the same send() and recv() calls as Windows, also set the non-blocking
    // property on the socket descriptor here instead of passing MSG_DONTWAIT to send() and recv().
    const int oldFlags = fcntl(fd, F_GETFL);
    if (oldFlags == -1) {
        ::close(fd);
        std::cerr << "IpSocket contruction failed D.\n";
        return;
    }
    fcntl(fd, F_SETFL, oldFlags & O_NONBLOCK);
#endif


    if (ok) {
        m_fd = fd;
    } else {
#ifdef _WIN32
        std::cerr << "IpSocket contruction failed E. Error is " << WSAGetLastError() << ".\n";
        closesocket(fd);
#else
        std::cerr << "IpSocket contruction failed E. Error is " << errno << ".\n";
        ::close(fd);
#endif
    }
}

IpSocket::IpSocket(FileDescriptor fd)
   : m_fd(fd)
{
}

IpSocket::~IpSocket()
{
    close();
#ifdef _WIN32
    WSACleanup();
#endif
}

void IpSocket::close()
{
    setEventDispatcher(nullptr);
    if (isValidFileDescriptor(m_fd)) {
#ifdef _WIN32
        closesocket(m_fd);
#else
        ::close(m_fd);
#endif
        m_fd = InvalidFileDescriptor;
    }
}

uint32 IpSocket::write(chunk a)
{
    if (!isValidFileDescriptor(m_fd)) {
        std::cerr << "\nIpSocket::write() failed A.\n\n";
        return 0; // TODO -1 and return int32?
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

chunk IpSocket::read(byte *buffer, uint32 maxSize)
{
    chunk ret;
    if (maxSize <= 0) {
        std::cerr << "\nIpSocket::read() failed A.\n\n";
        return ret;
    }

    ret.ptr = buffer;

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
    return isValidFileDescriptor(m_fd);
}

FileDescriptor IpSocket::fileDescriptor() const
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
