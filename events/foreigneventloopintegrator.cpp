// SPDX-FileCopyrightText: 2017 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "foreigneventloopintegrator.h"

#include "eventdispatcher_p.h"
#include "ieventpoller_p.h"
#include "iioeventlistener_p.h"

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
        m_fds.emplace(fd, 0);
        if (ioRw) {
            setReadWriteInterest(fd, ioRw);
        }
    }
}

void ForeignEventLoopIntegratorPrivate::removeFileDescriptor(FileDescriptor fd)
{
    if (!exiting) {
        setReadWriteInterest(fd, 0);
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

/** \class ForeignEventLoopIntegrator
    Allows to integrate Dferry event handling into another event loop.

    ForeignEventLoopIntegrator forwards I/O and timer event handling to another event loop
    owned by e.g. libuv, libevent, Qt, or GLib/GTK... so that Dferry can send and receive
    messages in an application whose main event loop is owned by such a framework.

    This is an abstract class - you need to add an implementation for the event loop it
    should integrate with.

    \see EventDispatcher
*/

/// Default constructor.
/// \see ~ForeignEventLoopIntegrator(), removeAllWatches()
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

/// Destructor.
/// Note: removeAllWatches() must be called from the derived class's destructor. When this destructor
/// runs, the subclass data has already been destroyed and the vtable is the one of this class,
/// so calling removeAllWatches() from this destructor would not work.
ForeignEventLoopIntegrator::~ForeignEventLoopIntegrator()
{
    d->exiting = true; // try to prevent surprising states during shutdown, including of d->m_fds
}

/// Call this in the destructor or other shutdown / reset code of your implementation.
/// It calls setWatchRead(fd, false), setWatchWrite(fd, false) and watchTimeout(-1) so as to
/// stop watching everything that was being watched.
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

/// \returns whether the event loop is exiting.
/// This is sometimes useful because some actions are neither necessary nor possible during
/// shutdown.
bool ForeignEventLoopIntegrator::exiting() const
{
    return d->exiting;
}

/// \fn virtual void ForeignEventLoopIntegrator::watchTimeout(int msecs) = 0;
/// Timer integration.
/// The implementation should set up a timeout that expires after \p mescs milliseconds
/// and call handleTimout() upon expiration.

/// \fn virtual void ForeignEventLoopIntegrator::setWatchRead(int fd, bool doWatch) = 0;
/// Read notification integration.
/// The implementation should watch or stop watching (depending on \p doWatch)
/// file descriptor \p fd for read availability and call handleReadyRead() when it is
/// available. The model is level-triggered: Every time the foreign event loop runs and
/// there is still data available for reading, handleReadyRead() should be called.

/// \fn virtual void ForeignEventLoopIntegrator::setWatchWrite(int fd, bool doWatch) = 0;
/// Write notification integration.
/// The implementation should watch or stop watching (depending on \p doWatch)
/// file descriptor \p fd for write availability and call handleReadyWrite() when it is
/// available. The model is level-triggered: Every time the foreign/ event loop runs and
/// it is still possible to write data, handleReadyWrite() should be called.

/// Call this when the timeout set by watchTimeout() has expired.
/// \see \link watchTimeout() \endlink
void ForeignEventLoopIntegrator::handleTimeout()
{
    if (!d->exiting) {
        EventDispatcherPrivate::of(d->dispatcher())->triggerDueTimers();
    }
}

/// Call this when watched file descriptor \p fd is available for reading.
/// \see \link setWatchRead() \endlink
void ForeignEventLoopIntegrator::handleReadyRead(int fd)
{
    if (!d->exiting) {
        EventDispatcherPrivate::of(d->dispatcher())->notifyListenerForIo(fd, IO::RW::Read);
    }
}

/// Call this when watched file descriptor \p fd is available for writing.
/// \see \link setWatchWrite() \endlink
void ForeignEventLoopIntegrator::handleReadyWrite(int fd)
{
    if (!d->exiting) {
        EventDispatcherPrivate::of(d->dispatcher())->notifyListenerForIo(fd, IO::RW::Write);
    }
}
