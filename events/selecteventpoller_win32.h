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

#ifndef SELECTEVENTPOLLER_H
#define SELECTEVENTPOLLER_H

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

#endif // SELECTEVENTPOLLER_H
