#include "ieventdispatcher.h"

#include "iconnection.h"

#include <cstdio>

#define IEVENTDISPATCHER_DEBUG

using namespace std;

IEventDispatcher::~IEventDispatcher()
{
    map<FileDescriptor, IConnection*>::iterator it = m_connections.begin();
    for ( ; it != m_connections.end(); it = m_connections.begin() ) {
        it->second->setEventDispatcher(0);
    }
}

bool IEventDispatcher::addConnection(IConnection *conn)
{
    pair<map<FileDescriptor, IConnection*>::iterator, bool> insertResult;
    insertResult = m_connections.insert(make_pair(conn->fileDescriptor(), conn));
    return insertResult.second;
}

bool IEventDispatcher::removeConnection(IConnection *conn)
{
    return m_connections.erase(conn->fileDescriptor());
}

void IEventDispatcher::notifyConnectionForReading(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("IEventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
#endif
    }
}

void IEventDispatcher::notifyConnectionForWriting(FileDescriptor fd)
{
    std::map<int, IConnection *>::iterator it = m_connections.find(fd);
    if (it != m_connections.end()) {
        it->second->notifyRead();
    } else {
#ifdef IEVENTDISPATCHER_DEBUG
        // while interesting for debugging, this is not an error if a connection was in the epoll
        // set and disconnected in its notifyRead() or notifyWrite() implementation
        printf("IEventDispatcher::notifyWrite(): unhandled file descriptor %d.\n", fd);
#endif
    }
}
