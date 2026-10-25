// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef EPOLLEVENTPOLLER_P_H
#define EPOLLEVENTPOLLER_P_H

#include "ieventpoller_p.h"

class EpollEventPoller : public IEventPoller
{
public:
    EpollEventPoller(EventDispatcher *dispatcher);
    ~EpollEventPoller() override;
    IEventPoller::InterruptAction poll(int timeout) override;
    void interrupt(IEventPoller::InterruptAction) override;

    // reimplemented from IEventPoller
    void addFileDescriptor(FileDescriptor fd, uint32 ioRw) override;
    void removeFileDescriptor(FileDescriptor fd) override;
    void setReadWriteInterest(FileDescriptor fd, uint32 ioRw) override;

private:
    void notifyRead(int fd);

    int m_interruptPipe[2];
    FileDescriptor m_epollFd;
};

#endif // EPOLLEVENTPOLLER_P_H
