#include "platform.h"
#include "types.h"

#include <vector>

class IConnectionClient;
class IEventDispatcher;

class IConnection
{
public:
    // An IConnection subclass must have a file descriptor after construction and it must not change
    // except to the invalid file descriptor when disconnected.
    IConnection();
    virtual ~IConnection();

    // usually, the maximum sensible number of clients is two: one for reading and one for writing.
    // avoiding (independent) readers and writers blocking each other is good for IO efficiency.
    void addClient(IConnectionClient *client);
    void removeClient(IConnectionClient *client);

    virtual int availableBytesForReading() = 0;
    virtual array read(byte *buffer, int maxSize) = 0;
    virtual int write(array data) = 0;
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
    friend class IConnectionClient;
    void updateReadWriteInterest(); // called internally and from IConnectionClient

    IEventDispatcher *m_eventDispatcher;
    std::vector<IConnectionClient *> m_clients;
    bool m_isReadNotificationEnabled;
    bool m_isWriteNotificationEnabled;
};
