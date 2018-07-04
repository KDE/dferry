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
    // The listenAddress passed in
    ConnectAddress listenAddress() const;
    // The address clients can connect to, which may be (usually is...) different from serverAddress
    ConnectAddress concreteAddress() const;
    Error error() const; // TODO

    void setNewConnectionListener(INewConnectionListener *listener);
    INewConnectionListener *newConnectionListener() const;

private:
    friend class ServerPrivate;
    ServerPrivate *d;
};

#endif // SERVER_H
