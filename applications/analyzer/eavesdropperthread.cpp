/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#include "eavesdropperthread.h"

#include "arguments.h"
#include "connectaddress.h"
#include "eavesdroppermodel.h"
#include "error.h"
#include "eventdispatcher.h"
#include "localsocket.h"
#include "message.h"
#include "pendingreply.h"
#include "connection.h"

#include "../setupeavesdropping.h"

EavesdropperThread::EavesdropperThread(EavesdropperModel *model)
{
    // do not parent this to the model; it doesn't work across threads
    moveToThread(&m_thread);
    connect(this, SIGNAL(messageReceived(Message *, qint64)),
            model, SLOT(addMessage(Message *, qint64)), Qt::QueuedConnection);
    connect(&m_thread, SIGNAL(started()), SLOT(run()));
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
