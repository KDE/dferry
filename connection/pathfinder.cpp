#include "pathfinder.h"

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

SessionBusInfo::SessionBusInfo(string str)
   : addressType(InvalidAddress)
{
    AddressType provisionalType = InvalidAddress;

    string unixAddressPrefix = "unix:";
    if (str.find(unixAddressPrefix) == 0) {
        provisionalType = LocalSocketFile;
        str.erase(0, unixAddressPrefix.length());
    }

    // TODO is there any escaping?
    const std::vector<string> parts = split(str, ',');

    if (provisionalType == LocalSocketFile) {
        string pathLiteral = "path=";
        string abstractLiteral = "abstract=";
        // TODO what about "guid=..." and "tmpdir=..."?

        for (int i = 0; i < parts.size(); i++) {
            const string &part = parts[i];
            if (part.find(pathLiteral) == 0) {
                if (addressType != InvalidAddress) {
                    goto invalid; // error - duplicate path specification
                }
                addressType = LocalSocketFile;
                path = part.substr(pathLiteral.length());
            } else if (part.find(abstractLiteral) == 0) {
                if (addressType != InvalidAddress) {
                    goto invalid;
                }
                addressType = AbstractLocalSocket;
                // by adding the \0 that signals a virtual socket address (see man 7 unix on
                // a Linux system) here already, we don't need to pass a whole SessionBusInfo
                // to the LocalSocket constructor, which makes it more flexible.
                // this might need some re-thinking.
                path = string(1, '\0') + part.substr(abstractLiteral.length());
            }
        }
    }
    return;
invalid:
    addressType = InvalidAddress;
    path.clear();
}

SessionBusInfo::SessionBusInfo()
    : addressType(InvalidAddress)
{}


//static
SessionBusInfo PathFinder::sessionBusInfo()
{
    ifstream infoFile(sessionInfoFile().c_str());

    string line;

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

    return SessionBusInfo(line);
}
