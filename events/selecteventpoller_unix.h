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
#include <vector>

#include <sys/select.h>

class SelectEventPoller : public IEventPoller
{
public:
    SelectEventPoller(EventDispatcher *dispatcher);
    ~SelectEventPoller();
    IEventPoller::InterruptAction poll(int timeout) override;
    void interrupt(IEventPoller::InterruptAction) override;

    // reimplemented from IEventPoller
    void addIoEventListener(IioEventListener *iol) override;
    void removeIoEventListener(IioEventListener *iol) override;
    void setReadWriteInterest(IioEventListener *iol, bool read, bool write) override;

private:
    void notifyRead(int fd);
    void resetFdSets();

    struct RwEnabled {
        bool readEnabled : 1;
        bool writeEnabled : 1;
    };

    std::unordered_map<FileDescriptor, RwEnabled> m_fds;

    std::vector<int> m_readFds;
    std::vector<int> m_writeFds;

    fd_set m_readSet;
    fd_set m_writeSet;

    int m_interruptPipe[2];
};

#endif // SELECTEVENTPOLLER_H
