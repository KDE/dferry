/*
   Copyright (C) 2013, 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "connectioninfo.h"

#include "stringtools.h"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <pwd.h>
#include <unistd.h>

using namespace std;

static string homeDir()
{
    const char *home = getenv("HOME"); // this overrides the entry in /etc/passwd
    if (!home) {
        // from /etc/passwd (or a similar mechanism)
        // ### user's storage is static; consider using getpwuid_r though!
        struct passwd *user = getpwuid(getuid());
        if (user) {
            home = user->pw_dir;
        }
    }
    assert(home);
    return string(home);
}

static string sessionInfoFile()
{
    static const int numMachineUuidFilenames = 2;
    static const char *machineUuidFilenames[numMachineUuidFilenames] = {
        "/var/lib/dbus/machine-id",
        "/etc/machine-id"
    };

    string uuid;
    for (int i = 0; i < numMachineUuidFilenames && uuid.empty(); i++) {
        ifstream uuidFile(machineUuidFilenames[i]);
        uuidFile >> uuid;
        // TODO check that uuid consists of lowercase hex chars
    }
    if (uuid.length() != 32) {
        return string();
    }

    const char *displayChar = getenv("DISPLAY");
    if (!displayChar) {
        // TODO error message "no X11 session blah"
        return string();
    }
    string display = displayChar;
    // TODO from the original: "Note that we leave the hostname in the display most of the time"
    size_t lastColon = display.rfind(':');
    if (lastColon == string::npos) {
        return string();
    }
    display.erase(0, lastColon + 1);

    static const char *pathInHome = "/.dbus/session-bus/";
    string ret = homeDir() + pathInHome + uuid + '-' + display;
    return ret;
}

class ConnectionInfo::Private
{
public:
    Private()
       : m_bus(ConnectionInfo::Bus::None),
         m_socketType(ConnectionInfo::SocketType::None),
         m_role(ConnectionInfo::Role::None),
         m_port(-1)
    {}

    void fetchSessionBusInfo();
    void parseSessionBusInfo(std::string info);

    ConnectionInfo::Bus m_bus;
    ConnectionInfo::SocketType m_socketType;
    ConnectionInfo::Role m_role;
    std::string m_path;
    int m_port;
    std::string m_guid;
};

ConnectionInfo::ConnectionInfo()
   : d(new Private)
{
}

ConnectionInfo::ConnectionInfo(Bus bus)
   : d(new Private)
{
    d->m_bus = bus;
    d->m_role = Role::Client;

    if (bus == Bus::Session) {
        d->fetchSessionBusInfo();
    } else if (bus == Bus::System) {
        // TODO non-Linux
        d->m_socketType = SocketType::Unix;
        d->m_path = "/var/run/dbus/system_bus_socket";
    } else {
        assert(bus <= Bus::PeerToPeer);
    }
}

ConnectionInfo::ConnectionInfo(const ConnectionInfo &other)
   : d(new Private(*other.d))
{
}

ConnectionInfo &ConnectionInfo::operator=(const ConnectionInfo &other)
{
    if (this != &other) {
        *d = *other.d;
    }
    return *this;
}

ConnectionInfo::~ConnectionInfo()
{
    delete d;
    d = nullptr;
}

void ConnectionInfo::setBus(Bus bus)
{
    d->m_bus = bus;
}

ConnectionInfo::Bus ConnectionInfo::bus() const
{
    return d->m_bus;
}

void ConnectionInfo::setSocketType(SocketType socketType)
{
    d->m_socketType = socketType;
}

ConnectionInfo::SocketType ConnectionInfo::socketType() const
{
    return d->m_socketType;
}

void ConnectionInfo::setRole(Role role)
{
    d->m_role = role;
}

ConnectionInfo::Role ConnectionInfo::role() const
{
    return d->m_role;
}

void ConnectionInfo::setPath(const std::string &path)
{
    d->m_path = path;
}

string ConnectionInfo::path() const
{
    return d->m_path;
}

void ConnectionInfo::setPort(int port)
{
    d->m_port = port;
}

int ConnectionInfo::port() const
{
    return d->m_port;
}

string ConnectionInfo::guid() const
{
    return d->m_guid;
}


void ConnectionInfo::Private::fetchSessionBusInfo()
{
    string line;

    // TODO: on X, the spec requires a special way to find the session bus
    //       (but nobody seems to use it?)

    // try the environment variable
    const char *envAddress = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (envAddress) {
        line = envAddress;
    } else {
        // try it using a byzantine system involving files...
        ifstream infoFile(sessionInfoFile().c_str());
        const string busAddressPrefix = "DBUS_SESSION_BUS_ADDRESS=";
        while (getline(infoFile, line)) {
            // TODO do we need any of the other information in the file?
            if (line.find(busAddressPrefix) == 0 ) {
                line = line.substr(busAddressPrefix.length());
                break;
            }
        }
    }

    parseSessionBusInfo(line);
}

void ConnectionInfo::Private::parseSessionBusInfo(string info)
{
    SocketType provisionalType = SocketType::None;

    string unixAddressLiteral = "unix:";
    string guidLiteral = "guid=";

    if (info.find(unixAddressLiteral) == 0) {
        provisionalType = SocketType::Unix;
        info.erase(0, unixAddressLiteral.length());
    }

    // TODO is there any escaping?
    const vector<string> parts = split(info, ',');

    if (provisionalType == SocketType::Unix) {
        string pathLiteral = "path=";
        string abstractLiteral = "abstract=";
        // TODO what about "tmpdir=..."?

        for (const string &part : parts) {
            if (part.find(pathLiteral) == 0) {
                if (m_socketType != SocketType::None) {
                    goto invalid; // error - duplicate path specification
                }
                m_socketType = SocketType::Unix;
                m_path = part.substr(pathLiteral.length());
            } else if (part.find(abstractLiteral) == 0) {
                if (m_socketType != SocketType::None) {
                    goto invalid;
                }
                m_socketType = SocketType::AbstractUnix;
                m_path = part.substr(abstractLiteral.length());
            }
        }
    } else {
        // TODO
    }

    for (const string &part : parts) {
        if (part.find(guidLiteral) == 0) {
            m_guid = part.substr(guidLiteral.length());
        }
    }

    return;
invalid:
    m_socketType = SocketType::None;
    m_path.clear();
}
