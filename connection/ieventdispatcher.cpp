#include "ieventdispatcher.h"

#include "iconnection.h"

#include <cstdio>

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
        printf("IEventDispatcher::notifyRead(): unhandled file descriptor %d.\n", fd);
    }
}
