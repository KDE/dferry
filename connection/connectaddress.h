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

#ifndef CONNECTADDRESS_H
#define CONNECTADDRESS_H

#include "export.h"

#include <string>

// I think we don't need to bother with subclasses, which will add boilerplate
// while on the other hand all-in-one isn't particularly easy to misuse.

class DFERRY_EXPORT ConnectAddress
{
public:
    enum class StandardBus : unsigned char
    {
        System,
        Session
    };

    enum class Type : unsigned char
    {
        None = 0,
        UnixPath,
        UnixDir,
        RuntimeDir,
        TmpDir,
        AbstractUnixPath,
        Tcp = 6,
        Tcp4,
        Tcp6
    };

    enum class Role : unsigned char
    {
        None = 0,
        BusClient,
        // BusServer, // = 2, not implemented
        PeerClient = 3,
        PeerServer
    };

    ConnectAddress();
    // Intentionally not explicit; this constructor discovers the bus address (mostly ;) according to spec
    ConnectAddress(StandardBus bus);
    ConnectAddress(const ConnectAddress &other);
    ConnectAddress &operator=(const ConnectAddress &other);
    ~ConnectAddress();

    bool operator==(const ConnectAddress &other) const;
    bool operator!=(const ConnectAddress &other) const { return !(*this == other); }

    void setType(Type type);
    Type type() const;

    void setRole(Role role);
    Role role() const;

    void setPath(const std::string &path);
    std::string path() const; // only for Unix domain sockets

    void setPort(int port);
    int port() const; // only for TcpSocket

    void setGuid(const std::string &guid);
    std::string guid() const;

    // the dbus standard format strings don't contain information about role and bus type, so the setter
    // will not touch these and the getter will lose that information.
    bool setAddressFromString(const std::string &addr);
    std::string toString() const;

    bool isServerOnly() const;

    // TODO comparison operators

private:
    class Private;
    Private *d;
};

#endif // CONNECTADDRESS_H
