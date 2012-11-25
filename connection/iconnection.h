#include "platform.h"
#include "types.h"

class IConnectionClient;
class IEventDispatcher;

class IConnection
{
public:
    IConnection();
    virtual ~IConnection();

    void setClient(IConnectionClient *client);
    IConnectionClient *client() const;

    virtual int write(array data) = 0;
    virtual int availableBytesForReading() = 0;
    virtual array read(int maxLen = -1) = 0;

    virtual bool isOpen() = 0;
    virtual FileDescriptor fileDescriptor() const = 0;

    virtual void setEventDispatcher(IEventDispatcher *loop);
    virtual IEventDispatcher *eventDispatcher() const;

private:
    friend class IEventDispatcher;
    // called from the event dispatcher. might become necessary to make them virtual in case any
    // new connection type has special requirements.
    void notifyRead();
    void notifyWrite();

    IEventDispatcher *m_eventDispatcher;
    IConnectionClient *m_client;
};
