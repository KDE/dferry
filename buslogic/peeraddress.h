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

#ifndef PEERADDRESS_H
#define PEERADDRESS_H

#include "export.h"

#include <string>

// I think we don't need to bother with subclasses, which will add boilerplate
// while on the other hand all-in-one isn't particularly easy to misuse.

class DFERRY_EXPORT PeerAddress
{
public:
    enum PeerType
    {
        NoPeer = 0,
        SystemBus,
        SessionBus,
        DirectConnection
    };

    enum SocketType
    {
        NoSocket = 0,
        UnixSocket,
        AbstractUnixSocket,
        TcpSocket
    };

    PeerAddress();
    // Intentionally not explicit; it resolves the details of a bus address
    PeerAddress(PeerType bus);
    PeerAddress(const PeerAddress &other);
    ~PeerAddress();

    PeerType peerType() const;
    SocketType socketType() const;
    std::string path() const;
    int port() const; // only for TcpSocket
    std::string guid() const;

    PeerAddress &operator=(const PeerAddress &other);

    // TODO comparison operators

private:
    class Private;
    Private *d;
};

#endif
