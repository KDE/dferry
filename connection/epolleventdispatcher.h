#ifndef EPOLLEVENTDISPATCHER_H
#define EPOLLEVENTDISPATCHER_H

#include "ieventdispatcher.h"

#include <map>

class EpollEventDispatcher : public IEventDispatcher
{
public:
    EpollEventDispatcher();
    virtual ~EpollEventDispatcher();
    virtual void poll(int timeout = -1);

protected:
    // reimplemented from IEventDispatcher
    bool addConnection(IConnection *conn);
    bool removeConnection(IConnection *conn);
    void setReadWriteInterest(IConnection *conn, bool read, bool write);

private:
    void notifyRead(int fd);

    FileDescriptor m_epollFd;
};

#endif // EPOLLEVENTDISPATCHER_H
