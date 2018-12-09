/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

#ifndef ITRANSPORTLISTENER_H
#define ITRANSPORTLISTENER_H

#include "iovaluetypes.h"

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

#endif // ITRANSPORTLISTENER_H
