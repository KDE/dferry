// SPDX-FileCopyrightText: 2017 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "server.h"

#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher_p.h"
#include "icompletionlistener.h"
#include "iioeventforwarder_p.h"
#include "inewconnectionlistener.h"
#include "iserver_p.h"
#include "itransport_p.h"

#include <cassert>

class ServerPrivate : public IIoEventForwarder, public ICompletionListener
{
public:
    ServerPrivate(EventDispatcher *dispatcher);

    // IIOEventForwarder
    IO::Status handleIoReady(IO::RW rw) override;

    // ICompletionListener
    void handleCompletion(void *transportServer) override;

    ConnectAddress listenAddress;
    ConnectAddress concreteAddress;
    EventDispatcher *eventDispatcher;
    Server *server;
    INewConnectionListener *newConnectionListener;
    IServer *transportServer;
};

ServerPrivate::ServerPrivate(EventDispatcher *dispatcher)
   : IIoEventForwarder(EventDispatcherPrivate::of(dispatcher)),
     eventDispatcher(dispatcher)
{
}

IO::Status ServerPrivate::handleIoReady(IO::RW rw)
{
    const IO::Status ret = transportServer->handleIoReady(rw);
    // ### error handling? But there is no possible permanent error with an already listening socket.
    return ret;
}

void ServerPrivate::handleCompletion(void *task)
{
    assert(task == transportServer);
    (void) task;
    if (newConnectionListener) {
        newConnectionListener->handleNewConnection(server);
    }
}

/** \class Server
    Accepts peer-to-peer connections.

    Peer-to-peer connections do not go through a message bus, so they are not subject to any
    bus policies and processing overheads.

    Even though such connections are peer-to-peer, one side needs to wait for the other to
    connect, which is what %Server does.

    \see EventDispatcher, Connection
*/

/// Constructs a server using event dispatcher \p dispatcher to listen on address \p listenAddress.
/// \see \link concreteAddress() \endlink
Server::Server(EventDispatcher *dispatcher, const ConnectAddress &listenAddress)
   : d(new ServerPrivate(dispatcher))
{
#if 0
    if (ca.bus() == ConnectAddress::Bus::None || ca.socketType() == ConnectAddress::AddressType::None ||
        ca.role() == ConnectAddress::Role::None ||
        (ca.role() != ConnectAddress::Role::Server && ca.isServerOnly())) {
        cerr << "\nConnection: connection constructor Exit A\n\n";
        return;
    }
#endif
    d->listenAddress = listenAddress;
    d->server = this;
    d->newConnectionListener = nullptr;
    d->transportServer = IServer::create(listenAddress, &d->concreteAddress);
    if (d->transportServer && d->transportServer->isListening()) {
        d->addIoListener(d->transportServer);
        d->transportServer->setNewConnectionListener(d);
    } else {
        delete d->transportServer;
        d->transportServer = nullptr;
    }

}

Server::~Server()
{
    delete d->transportServer;

    delete d;
    d = nullptr;
}

/// Sets the listener for new connections being made to this server.
/// \see INewConnectionListener
void Server::setNewConnectionListener(INewConnectionListener *listener)
{
    d->newConnectionListener = listener;
}

/// \returns the listener for new connections being made to this server.
/// \see INewConnectionListener
INewConnectionListener *Server::newConnectionListener() const
{
    return d->newConnectionListener;
}

/// Takes ownership of and returns the next queued client connection.
/// \returns the next queued client connection or nullptr if the queue is empty.
Connection *Server::takeNextClient()
{
    // TODO proper error handling / propagation
    if (!d->transportServer) {
        return nullptr;
    }
    ITransport *newTransport = d->transportServer->takeNextClient();
    if (!newTransport) {
        return nullptr;
    }

    return new Connection(newTransport, d->eventDispatcher, d->concreteAddress);
}

/// \returns whether the server is listening for new client connections
bool Server::isListening() const
{
    return d->transportServer ? d->transportServer->isListening() : false;
}

/// \returns The listening address of this server.
/// This is the listenAddress that was passed to the constructor -
/// it may be incomplete / a wildcard and as such not a concrete address.
/// \see \link concreteAddress() \endlink
ConnectAddress Server::listenAddress() const
{
    return d->listenAddress;
}

/// \returns The concrete listening address of this server.
/// This may be different from listenAddress. Such an address may leave parts unspecified
/// that the server needs to fill in to make it usable. For example, the address may specify
/// a directory in which the server needs to choose a randome filename, or the address
/// may specify an IP address but no port.
ConnectAddress Server::concreteAddress() const
{
    return d->concreteAddress;
}
