/*
   Copyright (C) 2017 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "foreigneventloopintegrator.h"

#include "eventdispatcher_p.h"
#include "ieventpoller.h"
#include "iioeventlistener.h"

#include <cassert>
#include <unordered_map>

class ForeignEventLoopIntegratorPrivate : public IEventPoller
{
public:
    ForeignEventLoopIntegratorPrivate(ForeignEventLoopIntegrator *integrator, EventDispatcher *dispatcher);
    //virtual ~ForeignEventLoopIntegratorPrivate();

    IEventPoller::InterruptAction poll(int timeout = -1) override;
    // interrupt the waiting for events (from another thread)
    void interrupt(InterruptAction action) override;

    void addFileDescriptor(FileDescriptor fd, uint32 ioRw) override;
    void removeFileDescriptor(FileDescriptor fd) override;
    void setReadWriteInterest(FileDescriptor fd, uint32 ioRw) override;

    // public accessor for protected member variable
    EventDispatcher *dispatcher() const { return m_dispatcher; }

    bool exiting = false;
    ForeignEventLoopIntegrator *m_integrator;

    std::unordered_map<FileDescriptor, uint32 /* ioRW */> m_fds;
};

ForeignEventLoopIntegratorPrivate::ForeignEventLoopIntegratorPrivate(ForeignEventLoopIntegrator *integrator,
                                                                     EventDispatcher *dispatcher)
   : IEventPoller(dispatcher),
     m_integrator(integrator)
{
}

IEventPoller::InterruptAction ForeignEventLoopIntegratorPrivate::poll(int /* timeout */)
{
    // do nothing, it can't possibly work (and it is *sometimes* a benign error to call this)
    return IEventPoller::NoInterrupt;
}

void ForeignEventLoopIntegratorPrivate::interrupt(InterruptAction /* action */)
{
    // do nothing, it can't possibly work (and it is *sometimes* a benign error to call this)
}

void ForeignEventLoopIntegratorPrivate::addFileDescriptor(FileDescriptor fd, uint32 ioRw)
{
    if (!exiting) {
        m_fds.emplace(fd, ioRw);
    }
}

void ForeignEventLoopIntegratorPrivate::removeFileDescriptor(FileDescriptor fd)
{
    if (!exiting) {
        m_fds.erase(fd);
    }
}

void ForeignEventLoopIntegratorPrivate::setReadWriteInterest(FileDescriptor fd, uint32 ioRw)
{
    if (exiting) {
        return;
    }
    const uint32 oldRw = m_fds.at(fd);
    const bool oldRead = oldRw & uint32(IO::RW::Read);
    const bool read = ioRw & uint32(IO::RW::Read);
    if (oldRead != read) {
        m_integrator->setWatchRead(fd, read);
    }
    const bool oldWrite = oldRw & uint32(IO::RW::Write);
    const bool write = ioRw & uint32(IO::RW::Write);
    if (oldWrite != write) {
        m_integrator->setWatchWrite(fd, write);
    }
    m_fds.at(fd) = ioRw;
}

ForeignEventLoopIntegrator::ForeignEventLoopIntegrator()
   : d(nullptr)
{
}

IEventPoller *ForeignEventLoopIntegrator::connectToDispatcher(EventDispatcher *dispatcher)
{
    assert(!d); // this is a one-time operation
    d = new ForeignEventLoopIntegratorPrivate(this, dispatcher);
    return d;
}

ForeignEventLoopIntegrator::~ForeignEventLoopIntegrator()
{
    d->exiting = true; // try to prevent surprising states during shutdown, including of d->m_fds

    // removeAllWatches() must be called from a derived class that implements the in this class pure
    // virtual methods setWatchRead(), setWatchWrite(), and watchTimeout(). During destruction, the
    // subclass data has already been destroyed and the vtable is the one of this class.
    //removeAllWatches();
}

void ForeignEventLoopIntegrator::removeAllWatches()
{
    for (auto it = d->m_fds.begin(); it != d->m_fds.end(); ++it) {
        if (it->second & uint32(IO::RW::Read)) {
            setWatchRead(it->first, false);
        }
        if (it->second & uint32(IO::RW::Write)) {
            setWatchWrite(it->first, false);
        }
        it->second = 0;
    }
    watchTimeout(-1);
    if (d) {
        d->m_integrator = nullptr;
        delete d;
        d = nullptr;
    }
}

bool ForeignEventLoopIntegrator::exiting() const
{
    return d->exiting;
}

void ForeignEventLoopIntegrator::handleTimeout()
{
    if (!d->exiting) {
        EventDispatcherPrivate::get(d->dispatcher())->triggerDueTimers();
    }
}

void ForeignEventLoopIntegrator::handleReadyRead(int fd)
{
    if (!d->exiting) {
        EventDispatcherPrivate::get(d->dispatcher())->notifyListenerForIo(fd, IO::RW::Read);
    }
}

void ForeignEventLoopIntegrator::handleReadyWrite(int fd)
{
    if (!d->exiting) {
        EventDispatcherPrivate::get(d->dispatcher())->notifyListenerForIo(fd, IO::RW::Write);
    }
}
