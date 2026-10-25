// SPDX-FileCopyrightText: 2015 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef SELECTEVENTPOLLER_WIN32_P_H
#define SELECTEVENTPOLLER_WIN32_P_H

#include "ieventpoller.h"

#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef FD_SETSIZE
#error We must be able to set FD_SETSIZE - make sure that nothing else sets it!
#endif
#define FD_SETSIZE 1024
#include <winsock2.h>

// Windows select() diverges from "proper Unix" select() just enough to seriously hurt readability
// when handling the differences with ifdefs, so use a separate implementation.
// Besides, the fd_set from winsock2.h is actually an array of sockets (not a bitmap like on Unix), which
// can be exploited to achive poll()-like performance characteristics without dealing with the problems
// that WSAPoll() has. That is currently not implemented.

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

    FileDescriptor m_interruptSocket[2];
};

#endif // SELECTEVENTPOLLER_WIN32_P_H
