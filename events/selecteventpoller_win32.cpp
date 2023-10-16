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

thread_local static SelectEventPoller *tls_selectPoller = nullptr;

static SOCKET createInterruptSocket()
{
    SOCKET ret = socket(AF_INET, SOCK_DGRAM, 0);
    if (ret == INVALID_SOCKET) {
        std::cerr << "createInterruptSocket() Error A.\n";
        return ret;
    }
    unsigned long value = 1; // 0 blocking, != 0 non-blocking
    if (ioctlsocket(ret, FIONBIO, &value) != NO_ERROR) {
        // something along the lines of... WS_ERROR_DEBUG(WSAGetLastError());
        std::cerr << "createInterruptSocket() Error B.\n";
        closesocket(ret);
        return INVALID_SOCKET;
    }
    return ret;
}

VOID CALLBACK triggerInterruptSocket(ULONG_PTR dwParam)
{
    SelectEventPoller *const sep = tls_selectPoller;
    if (sep) {
        sep->doInterruptSocket(dwParam != 0);
    } else {
        std::cerr << "triggerInterruptSocket() ignoring (apparently) spurios APC!\n";
    }
}

void SelectEventPoller::doInterruptSocket(bool isStop)
{
    const IEventPoller::InterruptAction newAction = isStop ? IEventPoller::Stop
                                                           : IEventPoller::ProcessAuxEvents;
    if (newAction > m_interruptAction) {
        m_interruptAction = newAction;
    }

    // This runs from the blocked select(). Signal the socket by closing it, thus properly interrupting
    // the select().

    if (m_interruptSocket != INVALID_SOCKET) {
        // closesocket() may enter an alertable walt, which may run APCs, which may call us *again*.
        // Prevent that by clearing m_interruptSocket *before* possibly triggering the APC.
        SOCKET sock = m_interruptSocket;
        m_interruptSocket = INVALID_SOCKET;
        closesocket(sock); // <- recursion here
    }
}

SelectEventPoller::SelectEventPoller(EventDispatcher *dispatcher)
   : IEventPoller(dispatcher),
     m_selectThreadHandle(INVALID_HANDLE_VALUE),
     m_interruptSocket(INVALID_SOCKET),
     m_interruptAction(IEventPoller::NoInterrupt)
{
    // IFF there is still an asynchronous procedure call queued for this thread (which usually happens
    // in the mean thread), we want it to trigger nothing, in case any Winsock functions (say socket())...
    // enters an alertable wait.
    tls_selectPoller = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                         &m_selectThreadHandle,
                         0, FALSE, DUPLICATE_SAME_ACCESS)) {
          assert(false); // H4X, gross!
    }

    // XXX TOXIC BUG!! m_selectThreadHandle = GetCurrentThread();

    WSAData wsadata;
    // IPv6 requires Winsock v2.0 or better (but we're not using IPv6 - yet!)
    if (WSAStartup(MAKEWORD(2, 0), &wsadata) != 0) {
        return;
    }

    m_interruptSocket = createInterruptSocket();
    // stupid: flush any pending APCs from previous instances; closesocket() will do a brief alertable
    // wait, apparently.
    closesocket(m_interruptSocket);

    m_interruptSocket = createInterruptSocket();

    tls_selectPoller = this;
}

SelectEventPoller::~SelectEventPoller()
{
    tls_selectPoller = nullptr;
    if (m_interruptSocket != INVALID_SOCKET) {
        closesocket(m_interruptSocket);
    }
    CloseHandle(m_selectThreadHandle);
}

IEventPoller::InterruptAction SelectEventPoller::poll(int timeout)
{
    // Check if some other code called an alertable waiting function which ran our user APC,
    // which closed m_interruptSocket and set m_interruptAction. Process it here if so.
    IEventPoller::InterruptAction ret = m_interruptAction;
    if (ret != IEventPoller::NoInterrupt) {
        assert(m_interruptSocket == INVALID_SOCKET);
        m_interruptAction = IEventPoller::NoInterrupt;
        m_interruptSocket = createInterruptSocket(); // re-arm
        return ret;
    }

    resetFdSets();

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

    if (m_interruptSocket == INVALID_SOCKET) {
        assert(m_interruptAction != IEventPoller::NoInterrupt);
        m_interruptSocket = createInterruptSocket();
    }
    FD_SET(m_interruptSocket, &m_exceptSet);

    struct timeval tv;
    struct timeval *tvPointer = nullptr;
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvPointer = &tv;
    }

    // select!
    int numEvents = select(0, &m_readSet, &m_writeSet, &m_exceptSet, tvPointer);
    if (numEvents == -1) {
        std::cerr << "Error code is " << WSAGetLastError() << " and except set has "
                  << m_exceptSet.fd_count << " elements.\n";
    }

    // check for interruption
    ret = m_interruptAction;
    if (ret != IEventPoller::NoInterrupt) {
        assert(m_interruptSocket == INVALID_SOCKET);
        m_interruptAction = IEventPoller::NoInterrupt;
        m_interruptSocket = createInterruptSocket(); // re-arm
        //numEvents--; // 1) We got here because the interrupt socket's exceptfd became active.
        //                2) However, fds in the except set don't count as "sockets /ready/" for
        //                   the return value of select().
        return ret;
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
    FD_ZERO(&m_exceptSet);
}

void SelectEventPoller::interrupt(IEventPoller::InterruptAction action)
{
    assert(action == IEventPoller::ProcessAuxEvents || action == IEventPoller::Stop);
    const ULONG_PTR dwParam = action == IEventPoller::Stop ? 0x1 : 0x0;
    QueueUserAPC(triggerInterruptSocket, m_selectThreadHandle, dwParam);
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
