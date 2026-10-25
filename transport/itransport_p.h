// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ITRANSPORT_P_H
#define ITRANSPORT_P_H

#include "iioeventlistener_p.h"
#include "platform_p.h"
#include "types.h"

#include <vector>

class ConnectAddress;
class EventDispatcher;
class ITransportListener;
class SelectEventPoller;

class ITransport : public IIoEventListener
{
public:
    // An ITransport subclass must have a file descriptor after construction and it must not change
    // except to the invalid file descriptor when disconnected.
    ITransport();
    ~ITransport() override;

    // This listener interface is different from IIoEventSource / IIoEventListener because that one is
    // one source, several file descriptors, one file descriptor to one listener
    // this is one file descriptor, two channels, one channel (read or write) to one listener.
    void setReadListener(ITransportListener *listener);
    void setWriteListener(ITransportListener *listener);

    // fileDescriptors is optional and can be used so receive open file descriptors (on supported systems).
    virtual IO::Result read(byte *buffer, uint32 maxSize, std::vector<int> *fileDescriptors = nullptr) = 0;
    // data2 is optional and can be used for scatter-gather I/O.
    // fileDescriptors is optional and can be used so send open file descriptors (on supported systems). Must
    // be sent with the first byte of the message according to the DBus protocol spec.
    virtual IO::Result write(chunk data, const chunk *data2 = nullptr,
                             const std::vector<int> *fileDescriptors = nullptr) = 0;

    void close();
    virtual bool isOpen() = 0;

    uint32 supportedPassingUnixFdsCount() const { return m_supportedUnixFdsCount; }
    bool prefersMultiBufferSend() const { return m_preferMultiBufferSend; }

    IO::Status handleIoReady(IO::RW rw) override;

    // factory method - creates a suitable subclass to connect to address
    static ITransport *create(const ConnectAddress &connectAddress);

protected:
    virtual void platformClose() = 0;
    uint32 m_supportedUnixFdsCount = 0;
    bool m_preferMultiBufferSend = false;

private:
    void updateTransportIoInterest(); // "Transport" in name to avoid confusion with IIoEventSource
    friend class ITransportListener;
    friend class SelectEventPoller;

    ITransportListener *m_readListener = nullptr;
    ITransportListener *m_writeListener = nullptr;
};

#endif // ITRANSPORT_P_H
