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

#include "peeraddress.h"

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

class PeerAddress::Private
{
public:
    Private()
       : m_peerType(NoPeer),
         m_socketType(NoSocket),
         m_port(-1)
    {}

    void fetchSessionBusInfo();
    void parseSessionBusInfo(std::string info);

    PeerAddress::PeerType m_peerType;
    PeerAddress::SocketType m_socketType;
    std::string m_path;
    int m_port;
    std::string m_guid;
};

PeerAddress::PeerAddress()
   : d(new Private)
{
}

PeerAddress::PeerAddress(PeerType bus)
   : d(new Private)
{
    d->m_peerType = bus;
    if (bus == SessionBus) {
        d->fetchSessionBusInfo();
    } else if (bus == SystemBus) {
        // TODO non-Linux
        d->m_socketType = UnixSocket;
        d->m_path = "/var/run/dbus/system_bus_socket";
    } else {
        // TODO error
    }
}

PeerAddress::PeerAddress(const PeerAddress &other)
   : d(new Private(*other.d))
{
}

PeerAddress &PeerAddress::operator=(const PeerAddress &other)
{
    *d = *other.d;
    return *this;
}

PeerAddress::~PeerAddress()
{
    delete d;
    d = 0;
}

PeerAddress::PeerType PeerAddress::peerType() const
{
    return d->m_peerType;
}

PeerAddress::SocketType PeerAddress::socketType() const
{
    return d->m_socketType;
}

string PeerAddress::path() const
{
    return d->m_path;
}

int PeerAddress::port() const
{
    return d->m_port;
}

string PeerAddress::guid() const
{
    return d->m_guid;
}


void PeerAddress::Private::fetchSessionBusInfo()
{
    ifstream infoFile(sessionInfoFile().c_str());
    string line;

    // TODO: on X, the spec requires a special way to find the session bus

    // try the environment variable
    const char *envAddress = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (envAddress) {
        line = envAddress;
    } else {
        // try it using a byzantine system involving files...
        string busAddressPrefix = "DBUS_SESSION_BUS_ADDRESS=";
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

void PeerAddress::Private::parseSessionBusInfo(string info)
{
    SocketType provisionalType = NoSocket;

    string unixAddressLiteral = "unix:";
    string guidLiteral = "guid=";

    if (info.find(unixAddressLiteral) == 0) {
        provisionalType = UnixSocket;
        info.erase(0, unixAddressLiteral.length());
    }

    // TODO is there any escaping?
    const vector<string> parts = split(info, ',');

    if (provisionalType == UnixSocket) {
        string pathLiteral = "path=";
        string abstractLiteral = "abstract=";
        // TODO what about "tmpdir=..."?

        for (int i = 0; i < parts.size(); i++) {
            const string &part = parts[i];
            if (part.find(pathLiteral) == 0) {
                if (m_socketType != NoSocket) {
                    goto invalid; // error - duplicate path specification
                }
                m_socketType = UnixSocket;
                m_path = part.substr(pathLiteral.length());
            } else if (part.find(abstractLiteral) == 0) {
                if (m_socketType != NoSocket) {
                    goto invalid;
                }
                m_socketType = AbstractUnixSocket;
                m_path = part.substr(abstractLiteral.length());
            }
        }
    } else {
        // TODO
    }

    for (int i = 0; i < parts.size(); i++) {
        const string &part = parts[i];
        if (part.find(guidLiteral) == 0) {
            m_guid = part.substr(guidLiteral.length());
        }
    }

    return;
invalid:
    m_socketType = NoSocket;
    m_path.clear();
}
