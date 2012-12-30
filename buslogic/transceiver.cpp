#include "transceiver.h"

#include "authnegotiator.h"
#include "icompletionclient.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "pathfinder.h"

#include <iostream>

using namespace std;

Transceiver::Transceiver(IEventDispatcher *dispatcher)
   : m_client(0),
     m_receivingMessage(0),
     m_connection(0),
     m_mainThreadTransceiver(0),
     m_authNegotiator(0),
     m_eventDispatcher(0)
{
    SessionBusInfo sessionBusInfo = PathFinder::sessionBusInfo();
    cout << "session bus address type: " << sessionBusInfo.addressType << '\n';
    cout << "session bus path: " << sessionBusInfo.path << '\n';

    m_connection = new LocalSocket(sessionBusInfo.path);
    m_connection->setEventDispatcher(dispatcher);
    cout << "connection is " << (m_connection->isOpen() ? "open" : "closed") << ".\n";
    m_authNegotiator = new AuthNegotiator(m_connection);
    m_authNegotiator->setCompletionClient(this);
}

Transceiver::~Transceiver()
{
    delete m_connection;
    m_connection = 0;
    delete m_authNegotiator;
    m_authNegotiator = 0;
    // TODO delete m_receivingMessage ?
}

Message *Transceiver::sendAndAwaitReply(Message *m)
{
}

void Transceiver::sendAsync(Message *m)
{
    m_sendQueue.push_back(m);
    m->setCompletionClient(this);
    if (!m_authNegotiator && m_sendQueue.size() == 1) {
        m->writeTo(m_connection);
    }
}

IConnection *Transceiver::connection() const
{
    return m_connection;
}

ITransceiverClient *Transceiver::client() const
{
    return m_client;
}

void Transceiver::setClient(ITransceiverClient *client)
{
    m_client = client;
}

void Transceiver::notifyCompletion(void *task)
{
    if (m_authNegotiator) {
        assert(task == m_authNegotiator);
        delete m_authNegotiator;
        m_authNegotiator = 0;
        cout << "Authenticated.\n";
        if (!m_sendQueue.empty()) {
            m_sendQueue.front()->writeTo(m_connection);
        }
        receiveNextMessage();
    } else {
        if (!m_sendQueue.empty() && task == m_sendQueue.front()) {
            cout << "Sent message.\n";
            m_sendQueue.pop_front();
            if (!m_sendQueue.empty()) {
                m_sendQueue.front()->writeTo(m_connection);
            }
        } else {
            cout << "Received message.\n";
            assert(task == m_receivingMessage);
            Message *const receivedMessage = m_receivingMessage;
            receiveNextMessage();
            m_client->messageReceived(receivedMessage);
        }
    }
}

void Transceiver::receiveNextMessage()
{
    m_receivingMessage = new Message(/*invalid serial*/ 0);
    m_receivingMessage->setCompletionClient(this);
    m_receivingMessage->readFrom(m_connection);
}
