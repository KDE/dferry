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

#ifndef IEVENTDISPATCHER_H
#define IEVENTDISPATCHER_H

#include "platform.h"

#include <map>

class IConnection;

class IEventDispatcher
{
public:
    virtual ~IEventDispatcher();
    virtual void poll(int timeout = -1) = 0;

protected:
    friend class IConnection;
    virtual bool addConnection(IConnection *conn);
    virtual bool removeConnection(IConnection *conn);
    virtual void setReadWriteInterest(IConnection *conn, bool read, bool write) = 0;
    void notifyConnectionForReading(FileDescriptor fd);
    void notifyConnectionForWriting(FileDescriptor fd);

    std::map<FileDescriptor, IConnection*> m_connections;
};

#endif // IEVENTDISPATCHER_H
