#include "iconnection.h"

#include "ieventdispatcher.h"

IConnection::IConnection()
   : m_eventDispatcher(0)
{
}

IConnection::~IConnection()
{
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
