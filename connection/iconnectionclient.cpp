#include "iconnectionclient.h"

#include "iconnection.h"
#include "ieventdispatcher.h"

IConnectionClient::IConnectionClient()
   : m_connection(0),
     m_isReadNotificationEnabled(false),
     m_isWriteNotificationEnabled(false)
{
}

IConnectionClient::~IConnectionClient()
{
    if (m_connection) {
        m_connection->removeClient(this);
    }
    m_connection = 0;
}

void IConnectionClient::setIsReadNotificationEnabled(bool enable)
{
    if (enable == m_isReadNotificationEnabled) {
        return;
    }
    m_isReadNotificationEnabled = enable;
    m_connection->updateReadWriteInterest();
}

bool IConnectionClient::isReadNotificationEnabled() const
{
    return m_isReadNotificationEnabled;
}

void IConnectionClient::setIsWriteNotificationEnabled(bool enable)
{
    if (enable == m_isWriteNotificationEnabled) {
        return;
    }
    m_isWriteNotificationEnabled = enable;
    m_connection->updateReadWriteInterest();
}

bool IConnectionClient::isWriteNotificationEnabled() const
{
    return m_isWriteNotificationEnabled;
}

void IConnectionClient::notifyConnectionReadyRead()
{
}

void IConnectionClient::notifyConnectionReadyWrite()
{
}

IConnection *IConnectionClient::connection() const
{
    return m_connection;
}
