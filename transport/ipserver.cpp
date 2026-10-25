// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "ipserver_p.h"

#include "connectaddress.h"
#include "ipresolver_p.h"

#include "icompletionlistener.h"
#include "ipsocket_p.h"

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

// TODO implement address family (IPv4 / IPv6) support
IpServer::IpServer(const ConnectAddress &ca)
   : m_listenFd(-1)
{
    assert(ca.type() == ConnectAddress::Type::Tcp);

    const FileDescriptor fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!isValidFileDescriptor(fd)) {
        std::cerr << "IpServer construction failed A.\n";
        return;
    }
#ifdef __unix__
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
        std::cerr << "IpServer construction failed B.\n";
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
#ifdef __unix__
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

#endif // RUNNING_DOXYGEN
