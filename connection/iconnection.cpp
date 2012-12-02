#include "iconnection.h"

#include "iconnectionclient.h"
#include "ieventdispatcher.h"

#include <algorithm>
#include <iostream>

using namespace std;

IConnection::IConnection()
   : m_eventDispatcher(0),
     m_isReadNotificationEnabled(false),
     m_isWriteNotificationEnabled(false)
{
}

IConnection::~IConnection()
{
    setEventDispatcher(0);
    vector<IConnectionClient *> clients = m_clients;
    while (!clients.empty()) {
        removeClient(clients.back()); // LIFO (stack-like) order seems safer...
    }
}

void IConnection::addClient(IConnectionClient *client)
{
    if (find(m_clients.begin(), m_clients.end(), client) != m_clients.end()) {
        return;
    }
    m_clients.push_back(client);
    client->m_connection = this;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void IConnection::removeClient(IConnectionClient *client)
{
    vector<IConnectionClient *>::iterator it = find(m_clients.begin(), m_clients.end(), client);
    if (it == m_clients.end()) {
        return;
    }
    m_clients.erase(it);
    client->m_connection = 0;
    if (m_eventDispatcher) {
        updateReadWriteInterest();
    }
}

void IConnection::updateReadWriteInterest()
{
    bool readInterest = false;
    bool writeInterest = false;
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isReadNotificationEnabled()) {
            readInterest = true;
        }
        if (m_clients[i]->isWriteNotificationEnabled()) {
            writeInterest = true;
        }
    }
    if (readInterest != m_isReadNotificationEnabled || writeInterest != m_isWriteNotificationEnabled) {
        m_isReadNotificationEnabled = readInterest;
        m_isWriteNotificationEnabled = writeInterest;
        m_eventDispatcher->setReadWriteInterest(this, m_isReadNotificationEnabled,
                                                m_isWriteNotificationEnabled);
    }
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
        m_isReadNotificationEnabled = false;
        m_isWriteNotificationEnabled = false;
        updateReadWriteInterest();
    }
}

IEventDispatcher *IConnection::eventDispatcher() const
{
    return m_eventDispatcher;
}

void IConnection::notifyRead()
{
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isReadNotificationEnabled()) {
            m_clients[i]->notifyConnectionReadyRead();
            break;
        }
    }
}

void IConnection::notifyWrite()
{
    for (int i = 0; i < m_clients.size(); i++) {
        if (m_clients[i]->isWriteNotificationEnabled()) {
            m_clients[i]->notifyConnectionReadyWrite();
            break;
        }
    }
}
