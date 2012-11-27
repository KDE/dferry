#ifndef IEVENTDISPATCHER_H
#define IEVENTDISPATCHER_H

#include "platform.h"

#include <map>

class IConnection;

class IEventDispatcher
{
public:
    virtual ~IEventDispatcher();
    virtual void poll(int timeout = -1) = 0;

protected:
    friend class IConnection;
    virtual bool addConnection(IConnection *conn);
    virtual bool removeConnection(IConnection *conn);
    void notifyConnectionForReading(FileDescriptor fd);

    std::map<FileDescriptor, IConnection*> m_connections;
};

#endif // IEVENTDISPATCHER_H
