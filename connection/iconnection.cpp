#include "iconnection.h"

#include "iconnectionclient.h"
#include "ieventdispatcher.h"

IConnection::IConnection()
   : m_eventDispatcher(0),
     m_client(0)
{
}

IConnection::~IConnection()
{
}

void IConnection::setClient(IConnectionClient *client)
{
    m_client = client;
}

IConnectionClient *IConnection::client() const
{
    return m_client;
}

void IConnection::setEventDispatcher(IEventDispatcher *ed)
{
    if (m_eventDispatcher == ed) {
        return;
    }
    if (m_eventDispatcher) {
        m_eventDispatcher->removeConnection(this);
    }
    m_eventDispatcher = ed;
    if (m_eventDispatcher) {
        m_eventDispatcher->addConnection(this);
    }
}

IEventDispatcher *IConnection::eventDispatcher() const
{
    return m_eventDispatcher;
}

void IConnection::notifyRead()
{
    if (m_client) {
        m_client->notifyConnectionReadyRead();
    }
}

void IConnection::notifyWrite()
{
    if (m_client) {
        m_client->notifyConnectionReadyWrite();
    }
}
