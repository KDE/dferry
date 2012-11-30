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
    virtual array read(byte *buffer, int maxSize) = 0;
    virtual void close() = 0;

    virtual bool isOpen() = 0;
    virtual FileDescriptor fileDescriptor() const = 0;

    virtual void setEventDispatcher(IEventDispatcher *loop);
    virtual IEventDispatcher *eventDispatcher() const;

protected:
    friend class IEventDispatcher;
    // called from the event dispatcher. virtual because at least LocalSocket requires extra logic.
    virtual void notifyRead();
    virtual void notifyWrite();

private:
    IEventDispatcher *m_eventDispatcher;
    IConnectionClient *m_client;
};
