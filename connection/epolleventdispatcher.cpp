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

void EpollEventDispatcher::poll(/*int timeout*/)
{
    static const int maxEvPerPoll = 8;
    static const int timeout = 0;
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
    }
}

bool EpollEventDispatcher::addConnection(IConnection *connection)
{
    if (!IEventDispatcher::addConnection(connection)) {
        return false;
    }
    struct epoll_event epevt;
    epevt.events = EPOLLIN;
    epevt.data.u64 = 0;
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
    // the assertion is technically not necessary because Connection should call us *before*
    // resetting its fd on failure, and we don't need the fd to remove the Connection.
    assert(connFd >= 0);
    // connFd will be removed from the epoll set automatically when it is closed - if there are
    // no copies of it made using e.g. dup(). better safe than sorry anyway...
    epoll_ctl(m_epollFd, EPOLL_CTL_DEL, connFd, 0);
    m_connections.erase(connFd);
    return true;
}
