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
    virtual int write(array data);
    virtual int availableBytesForReading();
    virtual array read(int maxSize = -1);
    virtual void close();
    virtual bool isOpen();
    virtual int fileDescriptor() const;
    virtual void notifyRead();
    // end IConnection

private:
    friend class IEventLoop;
    friend class IConnectionListener;
    // IConnectionListener uses this constructor for incoming connections
    LocalSocket(int fd);

    LocalSocket(); // not implemented
    LocalSocket(const LocalSocket &); // not implemented, disable copying
    LocalSocket &operator=(const LocalSocket &); // dito

    int m_fd;
    IEventLoop *m_eventLoop;
};

#endif // LOCALSOCKET_H
