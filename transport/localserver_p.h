// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef LOCALSERVER_P_H
#define LOCALSERVER_P_H

#include "iserver_p.h"

#include <string>

class LocalServer : public IServer
{
public:
    // This is for now intended only for client to client connections, so UID (via SCM_CREDENTIALS)
    // is not checked - instead socketFilePath should only be accessible by the appropriate user(s).
    LocalServer(const std::string &socketFilePath);
    ~LocalServer() override;

    bool isListening() const override;

    void platformClose() override;

    FileDescriptor fileDescriptor() const override;

    IO::Status handleIoReady(IO::RW rw) override;

private:
    int m_listenFd;
};

#endif // LOCALSERVER_P_H
