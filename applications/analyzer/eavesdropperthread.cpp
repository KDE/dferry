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
#include "connectioninfo.h"
#include "eavesdroppermodel.h"
#include "error.h"
#include "eventdispatcher.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

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
    delete m_transceiver;
    delete m_dispatcher;
}

static Message createEavesdropMessage(const char *messageType)
{
    Message ret = Message::createCall("/org/freedesktop/DBus", "org.freedesktop.DBus", "AddMatch");
    ret.setDestination("org.freedesktop.DBus");
    Arguments::Writer writer;
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    ret.setArguments(writer.finish());
    return ret;
}

void EavesdropperThread::run()
{
    m_timer.start();
    m_dispatcher = new EventDispatcher;

    m_transceiver = new Transceiver(m_dispatcher, ConnectionInfo::Bus::Session);
    m_transceiver->setSpontaneousMessageReceiver(this);
    {
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            m_transceiver->sendNoReply(createEavesdropMessage(messageType[i]));
        }
    }

    Q_ASSERT(m_transceiver->isConnected());

    while (m_dispatcher->poll()) {
    }

    m_thread.quit();
}

void EavesdropperThread::spontaneousMessageReceived(Message message)
{
    emit messageReceived(new Message(std::move(message)), m_timer.nsecsElapsed());
}
