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

#include "eavesdroppermodel.h"
#include "eventdispatcher.h"
#include "localsocket.h"
#include "message.h"
#include "peeraddress.h"
#include "transceiver.h"

EavesdropperThread::EavesdropperThread(EavesdropperModel *model)
{
    // do not parent this to the model; it doesn't work across threads
    moveToThread(&m_thread);
    // ### verify that the connection is a QueuedConnection
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

static void fillEavesdropMessage(Message *spyEnable, const char *messageType)
{
    spyEnable->setType(Message::MethodCallMessage);
    spyEnable->setDestination(std::string("org.freedesktop.DBus"));
    spyEnable->setInterface(std::string("org.freedesktop.DBus"));
    spyEnable->setPath(std::string("/org/freedesktop/DBus"));
    spyEnable->setMethod(std::string("AddMatch"));
    ArgumentList argList;
    ArgumentList::Writer writer = argList.beginWrite();
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    writer.finish();
    spyEnable->setArgumentList(argList);
}

void EavesdropperThread::run()
{
    m_timer.start();
    m_dispatcher = new EventDispatcher;

    m_transceiver = new Transceiver(m_dispatcher, PeerAddress::SessionBus);
    m_transceiver->setClient(this);
    {
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            Message *spyEnable = new Message;
            fillEavesdropMessage(spyEnable, messageType[i]);
            m_transceiver->sendAsync(spyEnable);
        }
    }

    while (m_dispatcher->poll()) {
    }
    m_thread.quit();
}

void EavesdropperThread::messageReceived(Message *message)
{
    emit messageReceived(message, m_timer.nsecsElapsed());
}
