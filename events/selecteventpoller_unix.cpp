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

#include "selecteventpoller_unix.h"

#include "eventdispatcher_p.h"
#include "iioeventlistener.h"

// The "arm Keil MDK Middleware Network Component" has certain quirks that we cover here. For lack
// of a really good way to detect it, we assume that builds with the ARM compiler are for it.
#if defined(__CC_ARM) || defined(__ARMCC_VERSION)
#define KEIL_MDK_NETWORK
#endif

#ifndef KEIL_MDK_NETWORK
#include <fcntl.h>
#else
#include "socketpair.h"
#endif
#include <unistd.h>

#include <cassert>
#include <cstdio>


SelectEventPoller::SelectEventPoller(EventDispatcher *dispatcher)
   : IEventPoller(dispatcher)
{
#ifndef KEIL_MDK_NETWORK
    pipe2(m_interruptPipe, O_NONBLOCK);
#else
    // TODO really need to make socketpair reliable and safe!
    socketpair(m_interruptPipe);
    if (true /* if socketpair was successful */) {
        unsigned long value = 1; // 0 blocking, != 0 non-blocking
        for (int i = 0 ; i < 2; i++) {
            if (ioctlsocket(m_interruptPipe[i], FIONBIO, &value) != NO_ERROR) {
                return false;
            }
        }
    }

#endif
    resetFdSets();
}

SelectEventPoller::~SelectEventPoller()
{
    close(m_interruptPipe[0]);
    close(m_interruptPipe[1]);
}

IEventPoller::InterruptAction SelectEventPoller::poll(int timeout)
{
    IEventPoller::InterruptAction ret = IEventPoller::NoInterrupt;

    resetFdSets();

    int nfds = m_interruptPipe[0];

    // set up the interruption listener
    FD_SET(m_interruptPipe[0], &m_readSet);

    for (const auto &fdRw : m_fds) {
        if (fdRw.second & uint32(IO::RW::Read)) {
            nfds = std::max(nfds, fdRw.first);
            FD_SET(fdRw.first, &m_readSet);
        }
        if (fdRw.second & uint32(IO::RW::Write)) {
            nfds = std::max(nfds, fdRw.first);
            FD_SET(fdRw.first, &m_writeSet);
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
    nfds += 1; // see documentation of select()...
    int numEvents = select(nfds, &m_readSet, &m_writeSet, nullptr, tvPointer);

    // check for interruption
    if (FD_ISSET(m_interruptPipe[0], &m_readSet)) {
        // interrupt; read bytes from pipe to clear buffers and get the interrupt type
        ret = IEventPoller::ProcessAuxEvents;
        char buf;
        while (read(m_interruptPipe[0], &buf, 1) > 0) {
            if (buf == 'S') {
                ret = IEventPoller::Stop;
            }
        }
    }

    if (ret == IEventPoller::Stop) {
        // ### discarding the rest of the events, to avoid touching "dead" data while shutting down
        numEvents = 0;
    }

    // dispatch reads and writes
    if (numEvents < 0) {
        // TODO error handling
    } else {
        EventDispatcherPrivate* const edpriv = EventDispatcherPrivate::get(m_dispatcher);
        for (int i = 0; i < nfds && numEvents > 0; i++) {
            if (FD_ISSET(i, &m_readSet)) {
                edpriv->notifyListenerForIo(i, IO::RW::Read);
                numEvents--;
            }
            if (FD_ISSET(i, &m_writeSet)) {
                edpriv->notifyListenerForIo(i, IO::RW::Write);
                numEvents--;
            }
        }
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
    write(m_interruptPipe[1], &buf, 1);
}

void SelectEventPoller::addFileDescriptor(FileDescriptor fd, uint32 ioRw)
{
    // The main select specific part of registration is in setReadWriteInterest().
    // Here we just check fd limits.
    if (fd >= FD_SETSIZE) {
        // TODO error...
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
