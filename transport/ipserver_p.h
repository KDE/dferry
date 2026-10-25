// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IPSERVER_P_H
#define IPSERVER_P_H

#include "iserver_p.h"

#include <string>

class ConnectAddress;

class IpServer : public IServer
{
public:
    IpServer(const ConnectAddress &ca);
    ~IpServer() override;

    bool isListening() const override;

    void platformClose() override;

    FileDescriptor fileDescriptor() const override;

    IO::Status handleIoReady(IO::RW rw) override;

private:
    FileDescriptor m_listenFd;
};

#endif // IPSERVER_P_H
