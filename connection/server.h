// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef SERVER_H
#define SERVER_H

#include "export.h"

class ServerPrivate;
class ConnectAddress;
class Connection;
class Error;
class EventDispatcher;
class INewConnectionListener;

class DFERRY_EXPORT Server
{
public:
    Server(EventDispatcher *dispatcher, const ConnectAddress &listenAddress);
    ~Server();

    Connection *takeNextClient();

    bool isListening() const;

    ConnectAddress listenAddress() const;
    ConnectAddress concreteAddress() const;

    void setNewConnectionListener(INewConnectionListener *listener);
    INewConnectionListener *newConnectionListener() const;

private:
    friend class ServerPrivate;
    ServerPrivate *d;
};

#endif // SERVER_H
