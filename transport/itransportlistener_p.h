// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ITRANSPORTLISTENER_P_H
#define ITRANSPORTLISTENER_P_H

#include "iovaluetypes_p.h"

class ITransport;

class ITransportListener
{
public:
    ITransportListener();
    virtual ~ITransportListener();

    ITransport *readTransport() const;
    ITransport *writeTransport() const;

    // public mainly for testing purposes - only call if you know what you're doing
    // no-op default implementations are provided so you only need to reimplement what you need
    virtual IO::Status handleTransportCanRead();
    virtual IO::Status handleTransportCanWrite();

protected:
    uint32 m_ioInterest = 0;
    friend class ITransport;
private:
    void updateIoInterest(IO::RW which, bool enable);
    ITransport *m_readTransport = nullptr; // set from ITransport::setReadListener()
    ITransport *m_writeTransport = nullptr; // set from ITransport::setWriteListener()
};

#endif // ITRANSPORTLISTENER_P_H
