// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "eavesdropperthread.h"

#include "arguments.h"
#include "argumentsreader.h"
#include "argumentswriter.h"
#include "connectaddress.h"
#include "eavesdroppermodel.h"
#include "error.h"
#include "eventdispatcher.h"
#include "message.h"
#include "pendingreply.h"
#include "connection.h"

#include "../setupeavesdropping.h"

EavesdropperThread::EavesdropperThread(EavesdropperModel *model)
{
    // do not parent this to the model; it doesn't work across threads
    moveToThread(&m_thread);
    connect(this, &EavesdropperThread::messageReceived,
            model, &EavesdropperModel::addMessage, Qt::QueuedConnection);
    connect(&m_thread, &QThread::started, this, &EavesdropperThread::run);
    m_thread.start();
}

EavesdropperThread::~EavesdropperThread()
{
    m_dispatcher->interrupt();
    m_thread.wait();
    delete m_connection;
    delete m_dispatcher;
}

void EavesdropperThread::run()
{
    m_timer.start();
    m_dispatcher = new EventDispatcher;
    m_connection = new Connection(m_dispatcher, ConnectAddress::StandardBus::Session);

    setupEavesdropping(m_connection);
    m_connection->setSpontaneousMessageReceiver(this);

    while (m_dispatcher->poll()) {
    }

    m_thread.quit();
}

void EavesdropperThread::handleSpontaneousMessageReceived(Message message, Connection *)
{
    emit messageReceived(new Message(std::move(message)), m_timer.nsecsElapsed());
}
