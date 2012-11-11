#ifndef PATHFINDER_H
#define PATHFINDER_H

#include <string>

struct SessionBusInfo
{
    SessionBusInfo();
    explicit SessionBusInfo(std::string spec);

    enum AddressType {
        InvalidAddress = 0,
        LocalSocketFile,
        AbstractLocalSocket
        // TODO more
    };
    AddressType addressType;
    std::string path;
};

// this class knows fixed server addresses and finds variable ones
class PathFinder
{
public:
    static SessionBusInfo sessionBusInfo();
};

#endif
