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

#include "epolleventpoller.h"

#include "eventdispatcher_p.h"
#include "iioeventlistener.h"

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>

EpollEventPoller::EpollEventPoller(EventDispatcher *dispatcher)
   : IEventPoller(dispatcher),
     m_epollFd(epoll_create(10))
{
    // set up a pipe that can interrupt the polling from another thread
    // (we could also use the Linux-only eventfd() - pipes are at least portable to epoll-like mechanisms)
    pipe2(m_interruptPipe, O_NONBLOCK);

    struct epoll_event epevt;
    epevt.events = EPOLLIN;
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = m_interruptPipe[0];
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, m_interruptPipe[0], &epevt);
}

EpollEventPoller::~EpollEventPoller()
{
    close(m_interruptPipe[0]);
    close(m_interruptPipe[1]);
    close(m_epollFd);
}

IEventPoller::InterruptAction EpollEventPoller::poll(int timeout)
{
    IEventPoller::InterruptAction ret = IEventPoller::NoInterrupt;

    static const int maxEvPerPoll = 8;
    struct epoll_event results[maxEvPerPoll];
    int nresults = epoll_wait(m_epollFd, results, maxEvPerPoll, timeout);
    if (nresults < 0) {
        // error?
        return ret;
    }

    for (int i = 0; i < nresults; i++) {
        struct epoll_event *evt = results + i;
        // Check the same notification conditions as select: a client can call read() or write() without
        // blocking if the socket was closed in some way.
        if (evt->events & (EPOLLIN | EPOLLERR | EPOLLHUP)) {
            if (evt->data.fd != m_interruptPipe[0]) {
                EventDispatcherPrivate::get(m_dispatcher)->notifyListenerForIo(evt->data.fd, IO::RW::Read);
            } else {
                // interrupt; read bytes from pipe to clear buffers and get the interrupt type
                ret = IEventPoller::ProcessAuxEvents;
                char buf;
                while (read(m_interruptPipe[0], &buf, 1) > 0) {
                    if (buf == 'S') {
                        ret = IEventPoller::Stop;
                    }
                }
                // ### discarding the rest of the events
                // this works in our currently only use case, interrupting poll once to reap a thread
                if (ret == IEventPoller::Stop) {
                    return ret;
                }
            }
        }
        if (evt->events & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
            EventDispatcherPrivate::get(m_dispatcher)->notifyListenerForIo(evt->data.fd, IO::RW::Write);
        }
    }
    return ret;
}

void EpollEventPoller::interrupt(IEventPoller::InterruptAction action)
{
    assert(action == IEventPoller::ProcessAuxEvents || action == IEventPoller::Stop);

    // write a byte to the write end so the poll waiting on the read end returns
    char buf = (action == IEventPoller::Stop) ? 'S' : 'N';
    write(m_interruptPipe[1], &buf, 1);
}

static uint32_t epeventsFromIoRw(uint32 ioRw)
{
    return ((ioRw & uint32(IO::RW::Read)) ? uint32(EPOLLIN) : 0) |
           ((ioRw & uint32(IO::RW::Write)) ? uint32(EPOLLOUT) : 0);
}

void EpollEventPoller::addFileDescriptor(FileDescriptor fd, uint32 ioRw)
{
    struct epoll_event epevt;
    epevt.events = epeventsFromIoRw(ioRw);
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = fd;
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, fd, &epevt);
}

void EpollEventPoller::removeFileDescriptor(FileDescriptor fd)
{
    // Connection should call us *before* resetting its fd on failure
    assert(fd >= 0);
    struct epoll_event epevt; // required in Linux < 2.6.9 even though it's ignored
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, fd, &epevt);
}

void EpollEventPoller::setReadWriteInterest(FileDescriptor fd, uint32 ioRw)
{
    if (!fd) {
        return;
    }
    struct epoll_event epevt;
    epevt.events = epeventsFromIoRw(ioRw);
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = fd;
    epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &epevt);
}
