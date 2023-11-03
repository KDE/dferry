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

#include "selecteventpoller_win32.h"

#include "eventdispatcher_p.h"
#include "iioeventlistener.h"

#include <iostream>
#include <thread>

#include <cassert>
#include <cstdio>

#include "socketpair.h"

SelectEventPoller::SelectEventPoller(EventDispatcher *dispatcher)
   : IEventPoller(dispatcher)
{
    WSAData wsadata;
    // IPv6 requires Winsock v2.0 or better (but we're not using IPv6 - yet!)
    if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0) {
        return;
    }

    socketpair(m_interruptSocket);
    unsigned long value = 1; // 0 blocking, != 0 non-blocking
    ioctlsocket(m_interruptSocket[0], FIONBIO, &value);
}

SelectEventPoller::~SelectEventPoller()
{
    closesocket(m_interruptSocket[0]);
    closesocket(m_interruptSocket[1]);
}

IEventPoller::InterruptAction SelectEventPoller::poll(int timeout)
{
    IEventPoller::InterruptAction ret = IEventPoller::NoInterrupt;

    resetFdSets();

    m_readSet.fd_array[m_readSet.fd_count++] = m_interruptSocket[0];

    // ### doing FD_SET "manually", avoiding a scan of the whole list for each set action - there is
    //     no danger of duplicates because our input is a set which already guarantees uniqueness.
    for (auto &fdRw : m_fds) {
        if (fdRw.second & uint32(IO::RW::Read)) {
            // FD_SET(fdRw.first, &m_readSet);
            if (m_readSet.fd_count < FD_SETSIZE) {
                m_readSet.fd_array[m_readSet.fd_count++] = fdRw.first;
            }
        }
        if (fdRw.second & uint32(IO::RW::Write)) {
            // FD_SET(fdRw.first, &m_writeSet);
            if (m_writeSet.fd_count < FD_SETSIZE) {
                m_writeSet.fd_array[m_writeSet.fd_count++] = fdRw.first;
            }
        }
    }

    struct timeval tv;
    struct timeval *tvPointer = nullptr;
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvPointer = &tv;
    }

    // select!
    int numEvents = select(0, &m_readSet, &m_writeSet, nullptr, tvPointer);
    if (numEvents == -1) {
        std::cerr << "select() failed with error code " << WSAGetLastError() << std::endl;
    }

    // check for interruption
    if (FD_ISSET(m_interruptSocket[0], &m_readSet)) {
        // interrupt; read bytes from pipe to clear buffers and get the interrupt type
        ret = IEventPoller::ProcessAuxEvents;
        char buf;
        while (recv(m_interruptSocket[0], &buf, 1, 0) > 0) {
            if (buf == 'S') {
                ret = IEventPoller::Stop;
            }
        }
    }

    // dispatch reads and writes
    if (numEvents < 0) {
        // TODO error handling ?
    }

    // This being Windows-specfic code, and with Windows's famous binary compatibility, we may
    // as well exploit that the Windows fd_set struct allows for relatively efficient querying
    // if you just iterate over its internal list, instead of searching the list for each file
    // descriptor like with FD_ISSET.
    // numEvents -= m_readSet.fd_count + m_writeSet.fd_count;
    for (uint i = 0; i < m_readSet.fd_count; i++) {
        EventDispatcherPrivate::get(m_dispatcher)
            ->notifyListenerForIo(m_readSet.fd_array[i], IO::RW::Read);
    }
    for (uint i = 0; i < m_writeSet.fd_count; i++) {
        EventDispatcherPrivate::get(m_dispatcher)
            ->notifyListenerForIo(m_writeSet.fd_array[i], IO::RW::Write);
    }

    return ret;
}

void SelectEventPoller::resetFdSets()
{
    FD_ZERO(&m_readSet);
    FD_ZERO(&m_writeSet);
}

void SelectEventPoller::interrupt(IEventPoller::InterruptAction action)
{
    assert(action == IEventPoller::ProcessAuxEvents || action == IEventPoller::Stop);
    // write a byte to the write end so the poll waiting on the read end returns
    char buf = (action == IEventPoller::Stop) ? 'S' : 'N';
    send(m_interruptSocket[1], &buf, 1, 0);
}

void SelectEventPoller::addFileDescriptor(FileDescriptor fd, uint32 ioRw)
{
    // The main select specific part of registration is in setReadWriteInterest().
    // Here we just check fd limits.
    if (m_fds.size() + 1 >= FD_SETSIZE) {
        std::cerr << "SelectEventPoller::addIoEventListener() failed: FD_SETSIZE too small.\n";
        // TODO indicate the error somehow?
        return;
    }

    m_fds.emplace(fd, ioRw);
}

void SelectEventPoller::removeFileDescriptor(FileDescriptor fd)
{
    m_fds.erase(fd);
}

void SelectEventPoller::setReadWriteInterest(FileDescriptor fd, uint32 ioRw)
{
    m_fds.at(fd) = ioRw;
}
