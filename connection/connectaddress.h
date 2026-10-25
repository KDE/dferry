// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef CONNECTADDRESS_H
#define CONNECTADDRESS_H

#include "export.h"

#include <string>

// I think we don't need to bother with subclasses, which will add boilerplate
// while on the other hand all-in-one isn't particularly easy to misuse.

class DFERRY_EXPORT ConnectAddress
{
public:
    /// Standard (at least on Linux systems) buses
    enum class StandardBus : unsigned char
    {
        System, ///< The system bus
        Session ///< The session bus
    };

    /// Address type
    enum class Type : unsigned char
    {
        None = 0, ///< None
        UnixPath, ///< Filesystem path to a UNIX local socket
        UnixDir, ///< Server only: random name under given directory
        RuntimeDir, ///< Server only: random name under XDG runtime directory
        TmpDir, ///< Server only; same as UnixDir, but may be an abstract (non-filesystem) directory
        AbstractUnixPath, ///< Linux only: Non-filesystem path to a UNIX local socket
        Tcp = 6, ///< TCP/IP (v4 or v6) address and port
        Tcp4, ///< TCP/IPv4 address and port
        Tcp6 ///< TCP/IPv6 address and port
    };

    /// %Role of the Connection that will use the address
    enum class Role : unsigned char
    {
        None = 0, ///< None
        BusClient, ///< Client on a message bus (everything except the daemon is a client)
        // BusServer, // = 2, not implemented
        PeerClient = 3, ///< Peer-to-peer client (the one who connects)
        PeerServer ///< Peer-to-peer server (the one who waits for a connection)
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

    void setHostname(const std::string &path);
    std::string hostname() const;

    void setPort(int port);
    int port() const; // only for TcpSocket

    void setGuid(const std::string &guid);
    std::string guid() const;

    bool setAddressFromString(const std::string &addr);
    std::string toString() const;

    bool isServerOnly() const;

    // TODO comparison operators that disregard irrelevant data

private:
    class Private;
    Private *d;
};

#endif // CONNECTADDRESS_H
