// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ISERVER_P_H
#define ISERVER_P_H

#include "iioeventlistener_p.h"
#include "platform_p.h"
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

#endif // ISERVER_P_H
