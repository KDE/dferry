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

#include "connectaddress.h"

#include "stringtools.h"

#include <cassert>
#include <cstdint>
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

#ifdef __unix__
static std::string homeDir()
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
    return std::string(home);
}

static std::string sessionInfoFile()
{
    static const int numMachineUuidFilenames = 2;
    static const char *machineUuidFilenames[numMachineUuidFilenames] = {
        "/var/lib/dbus/machine-id",
        "/etc/machine-id"
    };

    std::string uuid;
    for (int i = 0; i < numMachineUuidFilenames && uuid.empty(); i++) {
        std::ifstream uuidFile(machineUuidFilenames[i]);
        uuidFile >> uuid;
        // TODO check that uuid consists of lowercase hex chars
    }
    if (uuid.length() != 32) {
        return std::string();
    }

    const char *displayChar = getenv("DISPLAY");
    if (!displayChar) {
        // TODO error message "no X11 session blah"
        return std::string();
    }
    std::string display = displayChar;
    // TODO from the original: "Note that we leave the hostname in the display most of the time"
    size_t lastColon = display.rfind(':');
    if (lastColon == std::string::npos) {
        return std::string();
    }
    display.erase(0, lastColon + 1);

    static const char *pathInHome = "/.dbus/session-bus/";
    return homeDir() + pathInHome + uuid + '-' + display;
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
- Implement the nonce-data dialog in AuthClient / something it can use
*/
static std::string hashOfInstallRoot()
{
    // Using the non-Unicode API for bug compatibility with libdbus pathname hashes, for now.
    // This requires us to be installed to the same folder, which is a little weird, so maybe
    // drop this compatibility later.
    std::string ret;
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

    std::string pathString(path, pathLength);
    return sha1Hex(pathString);
}

// Returns something like:
// "tcp:host=localhost,port=52933,family=ipv4,guid=0fcf91a66520469005539fb2000001a7"
static std::string sessionBusAddressFromShm()
{
    std::string ret;
    std::string shmNamePostfix;

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
    std::string shmName = "DBusDaemonAddressInfo-";
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
        std::cerr << "Retrying OpenFileMappingA\n";
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

static std::string fetchSessionBusInfo()
{
    std::string ret;
#ifdef __unix__
    // TODO: on X, the spec requires a special way to find the session bus
    //       (but nobody seems to use it?)

    // try the environment variable
    const char *envAddress = getenv("DBUS_SESSION_BUS_ADDRESS");
    if (envAddress) {
        ret = envAddress;
    } else {
        // try it using a byzantine system involving files...
        std::ifstream infoFile(sessionInfoFile().c_str());
        const std::string busAddressPrefix = "DBUS_SESSION_BUS_ADDRESS=";
        while (getline(infoFile, ret)) {
            // TODO do we need any of the other information in the file?
            if (ret.find(busAddressPrefix) == 0 ) {
                ret = ret.substr(busAddressPrefix.length());
                break;
            }
        }
    }
#elif defined _WIN32
    ret = sessionBusAddressFromShm();
//#error see dbus-sysdeps-win.c, _dbus_get_autolaunch_shm and CreateMutexA / WaitForSingleObject in its callers
#endif // no #else <some error>, some platform might not have a session bus
    return ret;
}

static bool isSomeTcpType(ConnectAddress::Type t)
{
    return t == ConnectAddress::Type::Tcp || t == ConnectAddress::Type::Tcp4 ||
           t == ConnectAddress::Type::Tcp6;
}

class ConnectAddress::Private
{
public:
    Private()
       : m_addrType(ConnectAddress::Type::None),
         m_role(ConnectAddress::Role::None),
         m_port(-1)
    {}

    ConnectAddress::Type m_addrType;
    ConnectAddress::Role m_role;
    std::string m_path;
    int m_port;
    std::string m_guid;
};

ConnectAddress::ConnectAddress()
   : d(new Private)
{
}

ConnectAddress::ConnectAddress(StandardBus bus)
   : d(new Private)
{
    d->m_role = Role::BusClient;

    if (bus == StandardBus::Session) {
        setAddressFromString(fetchSessionBusInfo());
    } else {
        assert(bus == StandardBus::System);
#ifdef __unix__
        // ### does the __unix__ version actually apply to non-Linux?
        d->m_addrType = Type::UnixPath;
        d->m_path = "/var/run/dbus/system_bus_socket";
#else
        // Windows... it doesn't really have a system bus
        d->m_addrType = Type::None;
#endif
    }
}

ConnectAddress::ConnectAddress(const ConnectAddress &other)
   : d(new Private(*other.d))
{
}

ConnectAddress &ConnectAddress::operator=(const ConnectAddress &other)
{
    if (this != &other) {
        *d = *other.d;
    }
    return *this;
}

ConnectAddress::~ConnectAddress()
{
    delete d;
    d = nullptr;
}

bool ConnectAddress::operator==(const ConnectAddress &other) const
{
    // first, check everything that doesn't depend on address type
    if (d->m_addrType != other.d->m_addrType || d->m_role != other.d->m_role ||
        d->m_guid != other.d->m_guid) {
        return false;
    }
    // then check the data that matters for each address type (this is defensive coding, the irrelevant
    // data should be zero / null / empty).
    if (isSomeTcpType(d->m_addrType)) {
        return d->m_port == other.d->m_port;
    } else {
        return d->m_path == other.d->m_path;
    }
}

void ConnectAddress::setType(Type addrType)
{
    d->m_addrType = addrType;
}

ConnectAddress::Type ConnectAddress::type() const
{
    return d->m_addrType;
}

void ConnectAddress::setRole(Role role)
{
    d->m_role = role;
}

ConnectAddress::Role ConnectAddress::role() const
{
    return d->m_role;
}

void ConnectAddress::setPath(const std::string &path)
{
    d->m_path = path;
}

std::string ConnectAddress::path() const
{
    return d->m_path;
}

void ConnectAddress::setPort(int port)
{
    d->m_port = port;
}

int ConnectAddress::port() const
{
    return d->m_port;
}

void ConnectAddress::setGuid(const std::string &guid)
{
    d->m_guid = guid;
}

std::string ConnectAddress::guid() const
{
    return d->m_guid;
}

class UniqueCheck
{
public:
    enum Key
    {
        Path = 1 << 0,
        Host = 1 << 1,
        Port = 1 << 2,
        Family = 1 << 3,
        Guid = 1 << 4
    };

    inline bool claim(Key key)
    {
        const uint32_t oldClaimed = m_claimed;
        m_claimed |= key;
        return !(oldClaimed & key);
    }

private:
    uint32_t m_claimed = 0;
};

bool ConnectAddress::setAddressFromString(const std::string &addr)
{
    d->m_addrType = Type::None;
    d->m_path.clear();
    d->m_port = -1;
    d->m_guid.clear();

    const size_t addrStart = 0;
    const size_t addrEnd = addr.length();

    // ### The algorithm is a copy of libdbus's, which is kind of dumb (it parses each character
    // several times), but simple and works. This way, the errors for malformed input will be similar.

    // TODO: double check which strings may be empty in libdbus and emulate that
    // TODO: lots of off-by-one errors in length checks

    UniqueCheck unique;

    const size_t kvListStart = addr.find(':', addrStart);
    // ### if we're going to check the method immediately, we can omit the last condition
    if (kvListStart == std::string::npos || kvListStart >= addrEnd || kvListStart == addrStart) {
        return false;
    }
    const std::string method = addr.substr(addrStart, kvListStart - addrStart);

    if (method == "unix") {
        d->m_addrType = Type::UnixPath;
    } else if (method == "unixexec") {
        d->m_addrType = Type::UnixPath; // ### ???
    } else if (method == "tcp") {
        d->m_addrType = Type::Tcp;
    } else {
        return false;
    }

    size_t keyStart = kvListStart + 1;
    while (keyStart < addrEnd) {
        size_t valueEnd = addr.find(',', keyStart);
        if (valueEnd == std::string::npos) { // ### check zero length key-value pairs?
            valueEnd = addrEnd;
        }

        const size_t keyEnd = addr.find('=', keyStart);
        if (keyEnd == std::string::npos || keyEnd == keyStart) {
            return false;
        }
        const size_t valueStart = keyEnd + 1; // skip '='
        if (valueStart >= valueEnd) {
            return false;
        }

        const std::string key = addr.substr(keyStart, keyEnd - keyStart);
        const std::string value = addr.substr(valueStart, valueEnd - valueStart);

        // libdbus-1 takes the value from the first occurrence of a key because that is the simplest way to
        // handle it with its key list scanning approach. So it accidentally allows to specify the same key
        // multiple times... but it does not allow to specify contradictory keys, e.g. "path" and "abstract".
        // Instead of imitating that weirdness, we reject any address string with duplicate *or*
        // contradictory keys. By using the same "uniqueness category" Path for all the path-type keys, this
        // is easy to implement - and it makes more sense anyway.

        Type newAddressType = Type::None;
        if (key == "path") {
            newAddressType = Type::UnixPath;
        } else if (key == "abstract") {
            newAddressType = Type::AbstractUnixPath;
        } else if (key == "dir") {
            newAddressType = Type::UnixDir;
        } else if (key == "tmpdir") {
            newAddressType = Type::TmpDir;
        } else if (key == "runtime") {
            newAddressType = Type::RuntimeDir;
            if (value != "yes") {
                return false;
            }
        }

        if (newAddressType != Type::None) {
            if (!unique.claim(UniqueCheck::Path)) {
                return false;
            }
            if (d->m_addrType != Type::UnixPath) {
                return false;
            }
            d->m_addrType = newAddressType;
            if (newAddressType == Type::RuntimeDir) {
                // Ensure that no one somehow opens a socket called "yes"
                d->m_path.clear();
            } else {
                d->m_path = value;
            }
        } else if (key == "host") {
            if (!unique.claim(UniqueCheck::Host)) {
                return false;
            }
            if (!isSomeTcpType(d->m_addrType)) {
                return false;
            }
            if (value != "localhost" && value != "127.0.0.1") {
                return false;
            }
        } else if (key == "port") {
            if (!unique.claim(UniqueCheck::Port)) {
                return false;
            }
            if (!isSomeTcpType(d->m_addrType)) {
                return false;
            }
            const bool convertOk = dfFromString(value, &d->m_port);
            if (!convertOk || d->m_port < 1 || d->m_port > 65535) {
                return false;
            }
        } else if (key == "family") {
            if (!unique.claim(UniqueCheck::Family)) {
                return false;
            }
            if (d->m_addrType != Type::Tcp) {
                return false;
            }
            if (value == "ipv4") {
                d->m_addrType = Type::Tcp4;
            } else if (value == "ipv6") {
                d->m_addrType = Type::Tcp6;
            } else {
                return false;
            }
        } else if (key == "guid") {
            if (!unique.claim(UniqueCheck::Guid)) {
                return false;
            }
            d->m_guid = value;
        } else {
            return false;
        }

        keyStart = valueEnd + 1;
    }


    // Don't try to fully validate everything: the OS knows best how to fully check path validity, and
    // runtime errors still need to be handled in any case (e.g. access rights, etc)
    // ... what about the *Dir types, though?!
    if (d->m_addrType == Type::UnixPath || d->m_addrType == Type::AbstractUnixPath) {
        if (d->m_path.empty()) {
            return false;
        }
    } else if (isSomeTcpType(d->m_addrType)) {
        // port -1 is allowed for server-only addresses (the server picks a port)
        if (unique.claim(UniqueCheck::Host) /* we don't save the actual hostname */) {
            return false;
        }
    }

    return true;
}

#if 0
// Does anyone need this? The "try connections in order" things in the DBus spec seems ill-advised
// and nobody / nothing seems to be using it.

// static member function
std::vector<ConnectAddress> ConnectAddress::parseAddressList(const std::string &addrString)
{
    std::vector<ConnectAddress> ret;
    while (find next semicolon) {
        ConnectAddress addr;
        addr.setAddressFromString(substr);
        if (addr.error()) {
            return std::vector<ConnectAddress>();
        }
        ret.push_back(addr);
    }
    return ret;
}
#endif

std::string ConnectAddress::toString() const
{
    std::string ret;
    // no need to check bus and role, they are ignored here anyway
    // TODO consistency check between connectable vs listen address and role?

    switch (d->m_addrType) {
    case Type::UnixPath:
        ret = "unix:path=";
        break;
    case Type::AbstractUnixPath:
        ret = "unix:abstract=";
        break;
    case Type::UnixDir:
        ret = "unix:dir=";
        break;
    case Type::TmpDir:
        ret = "unix:tmpdir=";
        break;
    case Type::RuntimeDir:
        ret = "unix:runtime=yes";
        break;
    case Type::Tcp:
        ret = "tcp:host=localhost,port=";
        break;
    case Type::Tcp4:
        ret = "tcp:host=localhost,family=ipv4,port=";
        break;
    case Type::Tcp6:
        ret = "tcp:host=localhost,family=ipv6,port=";
        break;
    default:
        // invalid
        return ret;
    }

    if (isSomeTcpType(d->m_addrType)) {
        ret += dfToString(d->m_port);
    } else if (d->m_addrType != Type::RuntimeDir) {
        ret += d->m_path;
    }

    if (!d->m_guid.empty()) {
        ret += ",guid=";
        ret += d->m_guid;
    }

    return ret;
}

bool ConnectAddress::isServerOnly() const
{
    switch (d->m_addrType) {
#ifdef __unix__
    case Type::UnixDir: // fall through
    case Type::RuntimeDir: // fall through
#ifdef __linux__
    case Type::TmpDir:
#endif
    return true;
#endif
    case Type::Tcp:
        return d->m_port == -1;
    default:
        return false;
    }
}
