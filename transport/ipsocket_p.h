// SPDX-FileCopyrightText: 2015 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IPSOCKET_P_H
#define IPSOCKET_P_H

#include "itransport_p.h"

#include <string>

class ConnectAddress;

class IpSocket : public ITransport
{
public:
    // Connect to local socket at socketFilePath
    IpSocket(const ConnectAddress &ca);
    // Use an already open file descriptor
    IpSocket(FileDescriptor fd);

    ~IpSocket() override;

    // pure virtuals from ITransport
    IO::Result write(chunk data, const chunk *data2,
                     const std::vector<int> *fileDescriptors = nullptr) override;
    IO::Result read(byte *buffer, uint32 maxSize, std::vector<int> *fileDescriptors) override;
    void platformClose() override;
    bool isOpen() override;
    FileDescriptor fileDescriptor() const override;
    // end ITransport

    IpSocket() = delete;
    IpSocket(const IpSocket &) = delete;
    IpSocket &operator=(const IpSocket &) = delete;

private:
    FileDescriptor m_fd;
};

#endif // IPSOCKET_P_H
