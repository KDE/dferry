#include "platform.h"
#include "types.h"

class IEventDispatcher;

#if 0
class IConnectionReader
{
public:
    virtual ~IConnectionReader {}
    virtual void notifyRead() = 0;
};
#endif

class IConnection
{
public:
    IConnection();
    virtual ~IConnection();
    virtual int write(array data) = 0;
    virtual array read(int maxLen = -1) = 0;

    virtual bool isOpen() = 0;
    virtual FileDescriptor fileDescriptor() const = 0;

    virtual void setEventDispatcher(IEventDispatcher *loop);
    virtual IEventDispatcher *eventDispatcher() const;

private:
    friend class IEventDispatcher;
    // called from the event loop
    virtual void notifyRead() = 0;
    // virtual void notifyWrite() = 0; // for now we just block when the buffers are full
    
    IEventDispatcher *m_eventDispatcher;
};
