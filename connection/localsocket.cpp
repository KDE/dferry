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

#include "localsocket.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include "sys/uio.h"
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <climits>
#include <cstdlib>
#include <cstring>

// HACK, put this somewhere else (get the value from original d-bus? or is it infinite?)
static const int maxFds = 12;

using namespace std;

LocalSocket::LocalSocket(int fd)
   : m_fd(fd)
{}

LocalSocket::LocalSocket(const string &socketFilePath)
   : m_fd(-1)
{
    const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    // don't let forks inherit the file descriptor - that can cause confusion...
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un addr;
    addr.sun_family = PF_UNIX;
    bool ok = socketFilePath.length() < sizeof(addr.sun_path);
    if (ok) {
        memcpy(addr.sun_path, socketFilePath.c_str(), socketFilePath.length());
    }

    ok = ok && (connect(fd, (struct sockaddr *)&addr, sizeof(sa_family_t) + socketFilePath.length()) == 0);

    if (ok) {
        m_fd = fd;
    } else {
        ::close(fd);
    }
}

LocalSocket::~LocalSocket()
{
    close();
}

void LocalSocket::close()
{
    setEventDispatcher(0);
    if (m_fd >= 0) {
        ::close(m_fd);
    }
    m_fd = -1;
}

int LocalSocket::write(chunk a)
{
    if (m_fd < 0) {
        return 0; // TODO -1?
    }

    // sendmsg  boilerplate
    struct msghdr send_msg;
    struct iovec iov;

    send_msg.msg_name = 0;
    send_msg.msg_namelen = 0;
    send_msg.msg_flags = 0;
    send_msg.msg_iov = &iov;
    send_msg.msg_iovlen = 1;

    iov.iov_base = a.begin;
    iov.iov_len = a.length;

    // we can only send a fixed number of fds anyway due to the non-flexible size of the control message
    // receive buffer, so we set an arbitrary limit.
    const int numFds = 0; // TODO - how many should we get? do we need to know?
    assert(numFds <= maxFds);

    char cmsgBuf[CMSG_SPACE(sizeof(int) * maxFds)];

    if (numFds) {
        // fill in a control message
        send_msg.msg_control = cmsgBuf;
        send_msg.msg_controllen = CMSG_SPACE(sizeof(int) * numFds);

        struct cmsghdr *c_msg = CMSG_FIRSTHDR(&send_msg);
        c_msg->cmsg_len = CMSG_LEN(sizeof(int) * numFds);
        c_msg->cmsg_level = SOL_SOCKET;
        c_msg->cmsg_type = SCM_RIGHTS;

        // set the control data to pass - this is why we don't use the simpler write()
        for (int i = 0; i < numFds; i++) {
            // TODO
            // reinterpret_cast<int *>(CMSG_DATA(c_msg))[i] = message.m_fileDescriptors[i];
        }
    } else {
        // no file descriptor to send, no control message
        send_msg.msg_control = 0;
        send_msg.msg_controllen = 0;
    }

    while (iov.iov_len > 0) {
        int nbytes = sendmsg(m_fd, &send_msg, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // if we were notified for writing, we must have written at least one byte before getting
            // EAGAIN aka EWOULDBLOCK
            if (errno == EAGAIN && iov.iov_len < a.length) {
                break;
            }
            close();
            return false;
        }

        iov.iov_base = static_cast<char *>(iov.iov_base) + nbytes;
        iov.iov_len -= nbytes;
        // sendmsg() should always send the number of bytes asked for or block, so...
        if (nbytes != 0) {
            assert(iov.iov_len == 0);
        }
    }

    return a.length - iov.iov_len; // "- iov.iov_len" is for later, TODO revisit
}

int LocalSocket::availableBytesForReading()
{
    int available = 0;
    if (ioctl(m_fd, FIONREAD, &available) < 0) {
        available = 0;
    }
    return available;
}

chunk LocalSocket::read(byte *buffer, int maxSize)
{
    chunk ret;
    if (maxSize <= 0) {
        return ret;
    }

    // recvmsg-with-control-message boilerplate
    struct msghdr recv_msg;
    char cmsgBuf[CMSG_SPACE(sizeof(int) * maxFds)];
    memset(cmsgBuf, 0, sizeof(cmsgBuf));

    recv_msg.msg_control = cmsgBuf;
    recv_msg.msg_controllen = sizeof(cmsgBuf);
    recv_msg.msg_name = 0;
    recv_msg.msg_namelen = 0;
    recv_msg.msg_flags = 0;

    struct iovec iov;
    recv_msg.msg_iov = &iov;
    recv_msg.msg_iovlen = 1;

    // end boilerplate

    ret.begin = buffer;
    ret.length = 0;
    iov.iov_base = ret.begin;
    iov.iov_len = maxSize;
    while (iov.iov_len > 0) {
        int nbytes = recvmsg(m_fd, &recv_msg, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // if we were notified for reading, we must have read at least one byte before getting
            // EAGAIN aka EWOULDBLOCK
            if (errno == EAGAIN && iov.iov_len < maxSize) {
                break;
            }
            close();
            return ret;
        }
        ret.length += nbytes;
        iov.iov_base = static_cast<char *>(iov.iov_base) + nbytes;
        iov.iov_len -= nbytes;
    }

    // done reading "regular data", now read any file descriptors passed via control messages

    struct cmsghdr *c_msg = CMSG_FIRSTHDR(&recv_msg);
    if (c_msg && c_msg->cmsg_level == SOL_SOCKET && c_msg->cmsg_type == SCM_RIGHTS) {
        const int len = c_msg->cmsg_len / sizeof(int);
        int *data = reinterpret_cast<int *>(CMSG_DATA(c_msg));
        for (int i = 0; i < len; i++) {
            // TODO
            // message.appendFileDescriptor(data[i]);
        }
    }

    return ret;
}

bool LocalSocket::isOpen()
{
    return m_fd != -1;
}

int LocalSocket::fileDescriptor() const
{
    return m_fd;
}

void LocalSocket::notifyRead()
{
    if (availableBytesForReading()) {
        IConnection::notifyRead();
    } else {
        // This should really only happen in error cases! ### TODO test?
        close();
    }
}
