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

#ifndef LOCALSOCKET_H
#define LOCALSOCKET_H

#include "iconnection.h"

#include <string>

class IConnectionListener;

class LocalSocket : public IConnection
{
public:
    // Connect to local socket at socketFilePath
    LocalSocket(const std::string &socketFilePath);
    // Use an already open file descriptor
    LocalSocket(int fd);

    ~LocalSocket();

    // virtuals from IConnection
    uint32 write(chunk data) override;
    uint32 writeWithFileDescriptors(chunk data, const std::vector<int> &fileDescriptors) override;
    uint32 availableBytesForReading() override;
    chunk read(byte *buffer, uint32 maxSize) override;
    chunk readWithFileDescriptors(byte *buffer, uint32 maxSize, std::vector<int> *fileDescriptors) override;
    void close() override;
    bool isOpen() override;
    FileDescriptor fileDescriptor() const override;
    void handleCanRead() override;
    // end IConnection

    LocalSocket() = delete;
    LocalSocket(const LocalSocket &) = delete;
    LocalSocket &operator=(const LocalSocket &) = delete;

private:
    friend class IEventLoop;
    friend class IConnectionListener;

    int m_fd;
};

#endif // LOCALSOCKET_H
