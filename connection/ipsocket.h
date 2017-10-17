/*
   Copyright (C) 2015 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef IPSOCKET_H
#define IPSOCKET_H

#include "iconnection.h"

#include <string>

class IConnectionListener;
class ConnectionInfo;

class IpSocket : public IConnection
{
public:
    // Connect to local socket at socketFilePath
    IpSocket(const ConnectionInfo &ci);
    // Use an already open file descriptor
    IpSocket(FileDescriptor fd);

    ~IpSocket();

    // pure virtuals from IConnection
    uint32 write(chunk data) override;
    uint32 availableBytesForReading() override;
    chunk read(byte *buffer, uint32 maxSize) override;
    void close() override;
    bool isOpen() override;
    FileDescriptor fileDescriptor() const override;
    void handleCanRead() override;
    // end IConnection

    IpSocket() = delete;
    IpSocket(const IpSocket &) = delete;
    IpSocket &operator=(const IpSocket &) = delete;

private:
    friend class IEventLoop;
    friend class IConnectionListener;

    FileDescriptor m_fd;
};

#endif // IPSOCKET_H
