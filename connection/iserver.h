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

#ifndef ISERVER_H
#define ISERVER_H

#include "iioeventclient.h"
#include "platform.h"
#include "types.h"

#include <deque>

class ConnectionInfo;
class EventDispatcher;
class IConnection;
class ICompletionClient;

class IServer : public IioEventClient
{
public:
    IServer(); // TODO event dispatcher as constructor argument?
    virtual ~IServer();

    virtual bool isListening() const = 0;

    void setNewConnectionClient(ICompletionClient *client); // notified once on every new connection

    IConnection *takeNextConnection();
    virtual void close() = 0;

    virtual void setEventDispatcher(EventDispatcher *ed) override;
    virtual EventDispatcher *eventDispatcher() const override;

    static IServer *create(const ConnectionInfo &connectionInfo);

protected:
    friend class EventDispatcher;
    // notifyRead() and notifyWrite() from IioEventClient stay pure virtual

    std::deque<IConnection *> m_incomingConnections;
    ICompletionClient *m_newConnectionClient;

private:
    EventDispatcher *m_eventDispatcher;
};

#endif // ISERVER_H
