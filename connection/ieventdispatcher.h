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
    virtual void setReadWriteInterest(IConnection *conn, bool read, bool write) = 0;
    void notifyConnectionForReading(FileDescriptor fd);
    void notifyConnectionForWriting(FileDescriptor fd);

    std::map<FileDescriptor, IConnection*> m_connections;
};

#endif // IEVENTDISPATCHER_H
