#ifndef EPOLLEVENTDISPATCHER_H
#define EPOLLEVENTDISPATCHER_H

#include "ieventdispatcher.h"

#include <map>

class EpollEventDispatcher : public IEventDispatcher
{
public:
    EpollEventDispatcher();
    virtual ~EpollEventDispatcher();
    virtual void poll();

protected:
    // reimplemented from IEventDispatcher
    bool addConnection(IConnection *conn);
    bool removeConnection(IConnection *conn);

private:
    void notifyRead(int fd);

    FileDescriptor m_epollFd;
};

#endif // EPOLLEVENTDISPATCHER_H
