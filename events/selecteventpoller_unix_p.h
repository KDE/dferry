// SPDX-FileCopyrightText: 2015 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef SELECTEVENTPOLLER_UNIX_P_H
#define SELECTEVENTPOLLER_UNIX_P_H

#include "ieventpoller.h"

#include <unordered_map>

#include <sys/select.h>

class SelectEventPoller : public IEventPoller
{
public:
    SelectEventPoller(EventDispatcher *dispatcher);
    ~SelectEventPoller();
    IEventPoller::InterruptAction poll(int timeout) override;
    void interrupt(IEventPoller::InterruptAction) override;

    // reimplemented from IEventPoller
    void addFileDescriptor(FileDescriptor fd, uint32 ioRw) override;
    void removeFileDescriptor(FileDescriptor fd) override;
    void setReadWriteInterest(FileDescriptor fd, uint32 ioRw) override;

private:
    void notifyRead(int fd);
    void resetFdSets();

    std::unordered_map<FileDescriptor, uint32 /*ioRw*/> m_fds;

    fd_set m_readSet;
    fd_set m_writeSet;

    int m_interruptPipe[2];
};

#endif // SELECTEVENTPOLLER_UNIX_P_H
