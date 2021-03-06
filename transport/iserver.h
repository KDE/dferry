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

#include "iioeventlistener.h"
#include "platform.h"
#include "types.h"

#include <deque>

class ConnectAddress;
class EventDispatcher;
class ITransport;
class ICompletionListener;

class IServer : public IIoEventListener
{
public:
    IServer(); // TODO event dispatcher as constructor argument?
    ~IServer() override;

    virtual bool isListening() const = 0;

    void setNewConnectionListener(ICompletionListener *listener); // notified once on every new connection

    ITransport *takeNextClient();
    void close();

    // listenAddress may be a concrete address (in which case *concreteAddress will be set to a copy of it)
    // or it may be a "listen-only address", which is an underspecified or wildcard address. In the latter
    // case, *concreteAddress will be set to a concrete address generated according to listenAddress.
    static IServer *create(const ConnectAddress &listenAddress, ConnectAddress *concreteAddress);

protected:
    virtual void platformClose() = 0;

    friend class EventDispatcher;
    // handleCanRead() and handleCanWrite() from IioEventListener stay pure virtual

    std::deque<ITransport *> m_incomingConnections;
    ICompletionListener *m_newConnectionListener;
};

#endif // ISERVER_H
