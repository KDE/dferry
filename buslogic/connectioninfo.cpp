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

#ifdef __unix__
#include <pwd.h>
#include <unistd.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#include <mbstring.h>
#include "winutil.h"
#endif


using namespace std;

#ifdef __unix__
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
#endif

#ifdef _WIN32
/*
What do on Windows:
- Get the machine UUID (just for completeness or necessary is yet to be seen)
- Get the server bus address, which includes "noncefile=", the path to the nonce file.
  It is obtained from a shared memory segment with a well-known name, and the liveness
  of the server process is checked using a mutex AFAIU.
- Read 16 bytes from the nonce file, the credentials for the planned TCP connection
- Implement the nonce-data dialog in AuthNegotiator / something it can use
*/
static string hashOfInstallRoot()
{
    // Using the non-Unicode API for bug compatibility with libdbus pathname hashes, for now.
    // This requires us to be installed to the same folder, which is a little weird, so maybe
    // drop this compatibility later.
    string ret;
#if 1
    char path[MAX_PATH * 2] = "C:\\Program Files (x86)\\D-Bus\\bin\\dbus-monitor.exe)";
    size_t pathLength = 0;
#else
    char path[MAX_PATH * 2]; // * 2 because of multibyte (limited to double byte really) charsets
    HMODULE hm = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCSTR>(&hashOfInstallRoot), &hm);
    DWORD pathLength = GetModuleFileNameA(hm, path, sizeof(path));
    if (!pathLength) {
        // blah
        return ret;
    }
#endif
    // remove binary name to obtain the path
    char *lastBackslash = reinterpret_cast<char *>(
                            _mbsrchr(reinterpret_cast<unsigned char *>(path), '\\'));
    if (!lastBackslash) {
        return ret;
    }
    pathLength = lastBackslash - path + 1;

    // remove possible "\bin", "\bin\debug", "\bin\release"
    if (pathLength >= 4 && _strnicmp(lastBackslash - 4, "\\bin", 4) == 0) {
        pathLength -= 4;
    } else if (pathLength >= 10 && _strnicmp(lastBackslash - 10, "\\bin\\debug", 10) == 0) {
        pathLength -= 10;
    } else if (pathLength >= 12 && _strnicmp(lastBackslash - 12, "\\bin\\release", 12) == 0) {
        pathLength -= 12;
    }

    // super crappy tolower(), also known as _dbus_string_tolower_ascii()
    for (size_t i = 0; i < pathLength; i++) {
        if (path[i] >= 'A' && path[i] <= 'Z') {
            path[i] += 'a' - 'A';
        }
    }

    string pathString(path, pathLength);
    return sha1Hex(pathString);
}

// Returns something like:
// "tcp:host=localhost,port=52933,family=ipv4,guid=0fcf91a66520469005539fb2000001a7"
static string sessionBusAddressFromShm()
{
    string ret;
    string shmNamePostfix;

    if (true /* scope == "*install-path" */) {
        shmNamePostfix = hashOfInstallRoot();
    } else {
        // scope == "*user"
        shmNamePostfix = fetchWindowsSid();
    }

    // the SID corresponds to the "*user" "autolaunch method" or whatever apparently;
    // the default seems to be "install-path", for with the shm name postfix comes from
    // _dbus_get_install_root_as_hash - reimplement that one!

    // TODO check that the daemon is available using the mutex

    HANDLE sharedMem;
    string shmName = "DBusDaemonAddressInfo-";
    shmName += shmNamePostfix;
    // full shm name looks something like "DBusDaemonAddressInfo-395c81f0c8140cfdeab22831b0faf4ec0ebcaae5"
    // full mutex name looks something like "DBusDaemonMutex-395c81f0c8140cfdeab22831b0faf4ec0ebcaae5"

    // read shm
    for (int i = 0 ; i < 20; i++) {
        // we know that dbus-daemon is available, so we wait until shm is available
        sharedMem = OpenFileMappingA(FILE_MAP_READ, FALSE, shmName.c_str());
        if (sharedMem != 0) {
            break;
        }
        cerr << "Retrying OpenFileMappingA\n";
        Sleep(100);
    }

    if (sharedMem == 0)
        return ret;

    const void *addrView = MapViewOfFile(sharedMem, FILE_MAP_READ, 0, 0, 0);
    if (!addrView) {
        return ret;
    }
    ret = static_cast<const char *>(addrView);

    // cleanup
    UnmapViewOfFile(addrView);
    CloseHandle(sharedMem);

    return ret;
}
#endif


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
#ifdef __unix__
        // ### does the __unix__ version actually apply to non-Linux?
        d->m_socketType = SocketType::Unix;
        d->m_path = "/var/run/dbus/system_bus_socket";
#else
        // Windows... it doesn't really have a system bus
        d->m_socketType = SocketType::None;
#endif
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
#ifdef __unix__
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
#elif defined _WIN32
    line = sessionBusAddressFromShm();
//#error see dbus-sysdeps-win.c, _dbus_get_autolaunch_shm and CreateMutexA / WaitForSingleObject in its callers
#endif // no #else <some error>, some platform might not have a session bus
    parseSessionBusInfo(line);
}

void ConnectionInfo::Private::parseSessionBusInfo(string info)
{
    // typical input on Linux: "unix:abstract=/tmp/dbus-BrYfzr7UIv,guid=6c79b601925e949a9fe0c9ea565d80e8"
    // Windows: "tcp:host=localhost,port=64707,family=ipv4,guid=11ec225ce5f514366eec72f10000011d"

    // TODO is there any escaping?
    // ### well-formed input is assumed - this may produce nonsensical results with bad input
    const vector<string> parts = split(info, ',');

    const string guidLiteral = "guid=";
    const string tcpHostLiteral = "tcp:host=";
    const string portLiteral = "port=";
    // const string familyLiteral = "family="; // ### ignored for now (we assume "ipv4")
#ifdef __unix__
    const string unixPathLiteral = "unix:path=";
    const string unixAbstractLiteral = "unix:abstract=";
    // TODO what about "tmpdir=..."?

    for (const string &part : parts) {
        if (part.find(unixPathLiteral) == 0) {
            if (m_socketType != SocketType::None) {
                goto invalid; // error - duplicate path specification
            }
            m_socketType = SocketType::Unix;
            m_path = part.substr(unixPathLiteral.length());
        } else if (part.find(unixAbstractLiteral) == 0) {
            if (m_socketType != SocketType::None) {
                goto invalid;
            }
            m_socketType = SocketType::AbstractUnix;
            m_path = part.substr(unixAbstractLiteral.length());
        }
    }
#endif
    for (const string &part : parts) {
        if (part.find(guidLiteral) == 0) {
            m_guid = part.substr(guidLiteral.length());
        } else if (part.find(tcpHostLiteral) == 0) {
            if (part.substr(tcpHostLiteral.length()) != "localhost") {
                // only local connections are currently supported!
                goto invalid;
            }
            m_socketType = SocketType::Ip;
        } else if (part.find(portLiteral) == 0) {
            string portStr = part.substr(portLiteral.length());
            errno = 0;
            m_port = strtol(portStr.c_str(), nullptr, 10);
            if (errno) {
                goto invalid;
            }
        }
    }

    return;
invalid:
    // TODO introduce and call a clear() method
    m_socketType = SocketType::None;
    m_path.clear();
}
