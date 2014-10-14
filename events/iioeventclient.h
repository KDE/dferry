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

#ifndef IIOEVENTCLIENT_H
#define IIOEVENTCLIENT_H

#include "platform.h"

class EventDispatcher;
class EventDispatcherPrivate;

class IioEventClient
{
public:
    virtual ~IioEventClient();

    virtual FileDescriptor fileDescriptor() const = 0;

    virtual void setEventDispatcher(EventDispatcher *ed) = 0;
    virtual EventDispatcher *eventDispatcher() const = 0;

protected:
    friend class EventDispatcherPrivate;
    virtual void notifyRead() = 0;
    virtual void notifyWrite() = 0;
};

#endif // IIOEVENTCLIENT_H
