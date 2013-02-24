#include "epolleventdispatcher.h"

#include "iconnection.h"

#include <sys/epoll.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>

EpollEventDispatcher::EpollEventDispatcher()
 : m_epollFd(epoll_create(10))
{
}

EpollEventDispatcher::~EpollEventDispatcher()
{
    close(m_epollFd);
}

void EpollEventDispatcher::poll(int timeout)
{
    static const int maxEvPerPoll = 8;
    struct epoll_event results[maxEvPerPoll];
    int nresults = epoll_wait(m_epollFd, results, maxEvPerPoll, timeout);
    if (nresults < 0) {
        // error
        return;
    }

    for (int i = 0; i < nresults; i++) {
        struct epoll_event *evt = results + i;
        if (evt->events & EPOLLIN) {
            notifyConnectionForReading(evt->data.fd);
        }
        if (evt->events & EPOLLOUT) {
            notifyConnectionForWriting(evt->data.fd);
        }
    }
}

FileDescriptor EpollEventDispatcher::pollDescriptor() const
{
    return m_epollFd;
}

bool EpollEventDispatcher::addConnection(IConnection *connection)
{
    if (!IEventDispatcher::addConnection(connection)) {
        return false;
    }
    struct epoll_event epevt;
    epevt.events = 0;
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = connection->fileDescriptor();
    epoll_ctl(m_epollFd, EPOLL_CTL_ADD, connection->fileDescriptor(), &epevt);
    return true;
}

bool EpollEventDispatcher::removeConnection(IConnection *connection)
{
    if (!IEventDispatcher::removeConnection(connection)) {
        return false;
    }
    const int connFd = connection->fileDescriptor();
    // Connection should call us *before* resetting its fd on failure
    assert(connFd >= 0);
    struct epoll_event epevt; // required in Linux < 2.6.9 even though it's ignored
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, connFd, &epevt);
    return true;
}

void EpollEventDispatcher::setReadWriteInterest(IConnection *conn, bool readEnabled, bool writeEnabled)
{
    FileDescriptor fd = conn->fileDescriptor();
    if (!fd) {
        return;
    }
    struct epoll_event epevt;
    epevt.events = (readEnabled ? EPOLLIN : 0) | (writeEnabled ? EPOLLOUT : 0);
    epevt.data.u64 = 0; // clear high bits in the union
    epevt.data.fd = fd;
    epoll_ctl(m_epollFd, EPOLL_CTL_MOD, fd, &epevt);
}
