// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef LOCALSOCKET_P_H
#define LOCALSOCKET_P_H

#include "itransport_p.h"

#include <string>

class LocalSocket : public ITransport
{
public:
    // Connect to local socket at socketFilePath
    LocalSocket(const std::string &socketFilePath);
    // Use an already open file descriptor
    LocalSocket(int fd);

    ~LocalSocket() override;

    // virtuals from ITransport
    IO::Result write(chunk data, const chunk *data2,
                     const std::vector<int> *fileDescriptors = nullptr) override;
    IO::Result read(byte *buffer, uint32 maxSize, std::vector<int> *fileDescriptors) override;
    void platformClose() override;
    bool isOpen() override;
    FileDescriptor fileDescriptor() const override;
    // end ITransport

    LocalSocket() = delete;
    LocalSocket(const LocalSocket &) = delete;
    LocalSocket &operator=(const LocalSocket &) = delete;

private:
    int m_fd;
};

#endif // LOCALSOCKET_P_H
