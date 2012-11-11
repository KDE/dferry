#ifndef LOCALSOCKET_H
#define LOCALSOCKET_H

#include "iconnection.h"

#include <map>
#include <string>

class IEventLoop;
class IConnectionListener;
struct SessionBusInfo;

class LocalSocket : public IConnection
{
public:
    // Connect to local socket at socketFilePath
    LocalSocket(const std::string &socketFilePath);

    ~LocalSocket();

    // pure virtuals from IConnection
    int write(array data);
    array read(int maxSize = -1);
    bool isOpen();
    int fileDescriptor() const;
    // end IConnection

private:
    friend class IEventLoop;
    friend class IConnectionListener;
    // IConnectionListener uses this constructor for incoming connections
    LocalSocket(int fd);

    LocalSocket(); // not implemented
    LocalSocket(const LocalSocket &); // not implemented, disable copying
    LocalSocket &operator=(const LocalSocket &); // dito

    // pure virtual from IConnection
    void notifyRead();
    // end IConnection

    void doRead();

    void closeFd();

    int m_fd;
    IEventLoop *m_eventLoop;
};

#endif // LOCALSOCKET_H
