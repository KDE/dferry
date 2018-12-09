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

enum {
    // ### This is configurable in libdbus-1 but nobody ever seems to change it from the default of 16.
    MaxFds = 16,
    MaxFdPayloadSize = MaxFds * sizeof(int)
};

LocalSocket::LocalSocket(const std::string &socketFilePath)
   : m_fd(-1)
{
    m_supportsFileDescriptors = true;
    const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    // don't let forks inherit the file descriptor - that can cause confusion...
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un addr;
    addr.sun_family = PF_UNIX;
    bool ok = socketFilePath.length() + 1 <= sizeof(addr.sun_path);
    if (ok) {
        memcpy(addr.sun_path, socketFilePath.c_str(), socketFilePath.length() + 1);
    }

    ok = ok && (connect(fd, (struct sockaddr *)&addr, sizeof(sa_family_t) + socketFilePath.length()) == 0);

    if (ok) {
        m_fd = fd;
    } else {
        ::close(fd);
    }
}

LocalSocket::LocalSocket(int fd)
   : m_fd(fd)
{
}

LocalSocket::~LocalSocket()
{
    close();
}

void LocalSocket::platformClose()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

IO::Result LocalSocket::write(chunk data)
{
    IO::Result ret;
    if (data.length == 0) {
        return ret;
    }
    if (m_fd < 0) {
        ret.status = IO::Status::InternalError;
        return ret;
    }

    const uint32 initialLength = data.length;

    while (data.length > 0) {
        ssize_t nbytes = send(m_fd, data.ptr, data.length, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see EAGAIN comment in read()
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            ret.status = IO::Status::RemoteClosed;
            return ret;
        }

        data.ptr += nbytes;
        data.length -= size_t(nbytes);
    }

    ret.length = initialLength - data.length;
    return ret;
}

// TODO: consider using iovec to avoid "copying together" message parts before sending; iovec tricks
// are probably not going to help for receiving, though.
IO::Result LocalSocket::writeWithFileDescriptors(chunk data, const std::vector<int> &fileDescriptors)
{
    IO::Result ret;
    if (data.length == 0) {
        return ret;
    }
    if (m_fd < 0) {
        ret.status = IO::Status::InternalError;
        return ret;
    }

    // sendmsg  boilerplate
    struct msghdr send_msg;
    struct iovec iov;

    send_msg.msg_name = 0;
    send_msg.msg_namelen = 0;
    send_msg.msg_flags = 0;
    send_msg.msg_iov = &iov;
    send_msg.msg_iovlen = 1;

    iov.iov_base = data.ptr;
    iov.iov_len = data.length;

    // we can only send a fixed number of fds anyway due to the non-flexible size of the control message
    // receive buffer, so we set an arbitrary limit.
    const uint32 numFds = fileDescriptors.size();
    if (fileDescriptors.size() > MaxFds) {
        // TODO allow a proper error return
        close();
        ret.status = IO::Status::InternalError;
        return ret;
    }

    char cmsgBuf[CMSG_SPACE(MaxFdPayloadSize)];
    const uint32 fdPayloadSize = numFds * sizeof(int);

    if (numFds) {
        // fill in a control message
        send_msg.msg_control = cmsgBuf;
        send_msg.msg_controllen = CMSG_SPACE(fdPayloadSize);

        struct cmsghdr *c_msg = CMSG_FIRSTHDR(&send_msg);
        c_msg->cmsg_len = CMSG_LEN(fdPayloadSize);
        c_msg->cmsg_level = SOL_SOCKET;
        c_msg->cmsg_type = SCM_RIGHTS;

        // set the control data to pass - this is why we don't use the simpler write()
        int *const fdPayload = reinterpret_cast<int *>(CMSG_DATA(c_msg));
        for (uint32 i = 0; i < numFds; i++) {
            fdPayload[i] = fileDescriptors[i];
        }
    } else {
        // no file descriptor to send, no control message
        send_msg.msg_control = nullptr;
        send_msg.msg_controllen = 0;
    }

    while (iov.iov_len > 0) {
        ssize_t nbytes = sendmsg(m_fd, &send_msg, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see EAGAIN comment in read()
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            ret.status = IO::Status::RemoteClosed;
            break;
        } else if (nbytes > 0) {
            // control message already sent, don't send again
            send_msg.msg_control = nullptr;
            send_msg.msg_controllen = 0;
        }

        iov.iov_base = static_cast<char *>(iov.iov_base) + nbytes;
        iov.iov_len -= size_t(nbytes);
    }

    ret.length = data.length - iov.iov_len;
    return ret;
}

uint32 LocalSocket::availableBytesForReading()
{
    uint32 available = 0;
    if (ioctl(m_fd, FIONREAD, &available) < 0) {
        available = 0;
    }
    return available;
}

IO::Result LocalSocket::read(byte *buffer, uint32 maxSize)
{
    IO::Result ret;
    if (maxSize == 0) {
        return ret;
    }
    if (m_fd < 0) {
        ret.status = IO::Status::InternalError;
        return ret;
    }

    while (ret.length < maxSize) {
        ssize_t nbytes = recv(m_fd, buffer + ret.length, maxSize - ret.length, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // If we were notified for reading directly by the event dispatcher, we must be able to read at
            // least one byte before getting AGAIN aka EWOULDBLOCK - *however* the event loop might notify
            // something that tries to read everything (like Message::notifyRead()...) by calling read()
            // in a loop, and in that case, we may be called in an attempt to read more when there is
            // currently no more data, and it's not an error.
            // Just return zero bytes and no error in that case.
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
            return ret;
        }
        ret.length += size_t(nbytes);
    }

    return ret;
}

IO::Result LocalSocket::readWithFileDescriptors(byte *buffer, uint32 maxSize,
                                                std::vector<int> *fileDescriptors)
{
    IO::Result ret;
    if (maxSize == 0) {
        return ret;
    }
    if (m_fd < 0) {
        ret.status = IO::Status::InternalError;
        return ret;
    }

    // recvmsg-with-control-message boilerplate
    struct msghdr recv_msg;
    char cmsgBuf[CMSG_SPACE(sizeof(int) * MaxFds)];

    recv_msg.msg_control = cmsgBuf;
    recv_msg.msg_controllen = CMSG_SPACE(MaxFdPayloadSize);
    memset(cmsgBuf, 0, recv_msg.msg_controllen);
    // prevent equivalent to CVE-2014-3635 in libdbus-1: We could receive and ignore an extra file
    // descriptor, thus eventually run out of file descriptors
    recv_msg.msg_controllen = CMSG_LEN(MaxFdPayloadSize);
    recv_msg.msg_name = 0;
    recv_msg.msg_namelen = 0;
    recv_msg.msg_flags = 0;

    struct iovec iov;
    recv_msg.msg_iov = &iov;
    recv_msg.msg_iovlen = 1;

    // end boilerplate

    iov.iov_base = buffer;
    iov.iov_len = maxSize;
    while (iov.iov_len > 0) {
        ssize_t nbytes = recvmsg(m_fd, &recv_msg, MSG_DONTWAIT);
        if (nbytes < 0) {
            if (errno == EINTR) {
                continue;
            }
            // see comment in read()
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            close();
            ret.status = IO::Status::RemoteClosed;
            break;
        }  else if (nbytes == 0) {
            // orderly shutdown
            close();
            ret.status = IO::Status::RemoteClosed;
            break;
        } else {
            // read any file descriptors passed via control messages

            struct cmsghdr *c_msg = CMSG_FIRSTHDR(&recv_msg);
            if (c_msg && c_msg->cmsg_level == SOL_SOCKET && c_msg->cmsg_type == SCM_RIGHTS) {
                const int count = (c_msg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                const int *const fdPayload = reinterpret_cast<int *>(CMSG_DATA(c_msg));
                for (int i = 0; i < count; i++) {
                    fileDescriptors->push_back(fdPayload[i]);
                }
            }

            // control message already received, don't receive another
            recv_msg.msg_control = nullptr;
            recv_msg.msg_controllen = 0;
        }

        ret.length += size_t(nbytes);
        iov.iov_base = static_cast<char *>(iov.iov_base) + nbytes;
        iov.iov_len -= size_t(nbytes);
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
