// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "localserver_p.h"

#include "icompletionlistener.h"
#include "localsocket_p.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cassert>
#include <cstring>

LocalServer::LocalServer(const std::string &socketFilePath)
   : m_listenFd(-1)
{
    const int fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        return;
    }
    // don't let forks inherit the file descriptor - just in case
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    struct sockaddr_un addr;
    addr.sun_family = PF_UNIX;
    bool ok = socketFilePath.length() + 1 <= sizeof(addr.sun_path);
    if (ok) {
        memcpy(addr.sun_path, socketFilePath.c_str(), socketFilePath.length() + 1);
    }

    if (!socketFilePath.empty() && socketFilePath[0] != '\0') {
        // not a so-called abstract socket (weird but useful Linux specialty)
        unlink(socketFilePath.c_str());
    }
    ok = ok && (bind(fd, (struct sockaddr *)&addr, sizeof(sa_family_t) + socketFilePath.length()) == 0);
    ok = ok && (::listen(fd, /* max queued incoming connections */ 64) == 0);

    if (ok) {
        m_listenFd = fd;
    } else {
        ::close(fd);
    }
}

LocalServer::~LocalServer()
{
    close();
}

IO::Status LocalServer::handleIoReady(IO::RW rw)
{
    if (rw != IO::RW::Read) {
        assert(false);
        return IO::Status::InternalError;
    }
    if (m_listenFd < 0) {
        return IO::Status::LocalClosed;
    }
    int connFd = -1;
    while (true) {
        connFd = accept(m_listenFd, nullptr, nullptr);
        if (connFd >= 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        // After listen() succeeded, the only possible errors are invalid parameters (we don't do that,
        // right?), EINTR, out of resource errors (which can be temporary), or aborted connection
        // attempt. Just give up on this connection attempt and stay in listening state.
        return IO::Status::OK;
    }
    fcntl(connFd, F_SETFD, FD_CLOEXEC);

    m_incomingConnections.push_back(new LocalSocket(connFd));
    if (m_newConnectionListener) {
        m_newConnectionListener->handleCompletion(this);
    }
    return IO::Status::OK;
}

bool LocalServer::isListening() const
{
    return m_listenFd >= 0;
}

void LocalServer::platformClose()
{
    if (m_listenFd >= 0) {
        ::close(m_listenFd);
        m_listenFd = -1;
    }
}

FileDescriptor LocalServer::fileDescriptor() const
{
    return m_listenFd;
}

#endif // RUNNING_DOXYGEN
