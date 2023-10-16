/*
   Copyright (C) 2018 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "connectaddress.h"
#include "connection.h"
#include "eventdispatcher.h"

#include "../testutil.h"

#include <iostream>
#include <string>

static void testFromString()
{
#ifdef __unix__
    {
        ConnectAddress addr;
        addr.setAddressFromString("unix:path=/dev/null");
        TEST(addr.type() == ConnectAddress::Type::UnixPath);
        TEST(addr.path() == "/dev/null");
    }
    {
        ConnectAddress addr;
        addr.setAddressFromString("unix:abstract=less/traveled");
        TEST(addr.type() == ConnectAddress::Type::AbstractUnixPath);
        TEST(addr.path() == "less/traveled");
    }
    {
        ConnectAddress addr;
        addr.setAddressFromString("unix:guid=00000000000000000000000000000000,abstract=/tmp/dbus-XXXXXXXXXX");
        TEST(addr.type() == ConnectAddress::Type::AbstractUnixPath);
        TEST(addr.path() == "/tmp/dbus-XXXXXXXXXX");
        TEST(addr.guid() == "00000000000000000000000000000000");
    }
#endif
    {
        ConnectAddress addr;
        addr.setAddressFromString("tcp:port=2233,host=localhost,guid=10000000000000000000000000000000");
        TEST(addr.type() == ConnectAddress::Type::Tcp);
        TEST(addr.port() == 2233);
        TEST(addr.guid() == "10000000000000000000000000000000");
    }
    {
        ConnectAddress addr;
        addr.setAddressFromString("tcp:family=ipv4,host=127.0.0.1,port=65535");
        TEST(addr.type() == ConnectAddress::Type::Tcp4);
        TEST(addr.port() == 65535);
        TEST(addr.guid() == "");
    }
    {
        ConnectAddress addr;
        addr.setAddressFromString("tcp:host=localhost,port=1,family=ipv6");
        TEST(addr.type() == ConnectAddress::Type::Tcp6);
        TEST(addr.port() == 1);
        TEST(addr.guid() == "");
    }
    // TODO lots of ugly error cases
}

static void testToString()
{
#ifdef __unix__
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::UnixPath);
        addr.setPath("/dev/null");
        TEST(addr.toString() == "unix:path=/dev/null");
    }
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::AbstractUnixPath);
        addr.setPath("less/traveled");
        TEST(addr.toString() == "unix:abstract=less/traveled");
    }
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::AbstractUnixPath);
        addr.setPath("/tmp/dbus-XXXXXXXXXX");
        addr.setGuid("00000000000000000000000000000000");
        TEST(addr.toString() =="unix:abstract=/tmp/dbus-XXXXXXXXXX,guid=00000000000000000000000000000000");
    }
#endif
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::Tcp);
        addr.setPort(2233);
        addr.setGuid("10000000000000000000000000000000");
        TEST(addr.toString() == "tcp:host=localhost,port=2233,guid=10000000000000000000000000000000");
    }
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::Tcp4);
        addr.setPort(65535);
        TEST(addr.toString() == "tcp:host=localhost,family=ipv4,port=65535");
    }
    {
        ConnectAddress addr;
        addr.setType(ConnectAddress::Type::Tcp6);
        addr.setPort(1);
        TEST(addr.toString() == "tcp:host=localhost,family=ipv6,port=1");
    }
}

static void testFindBuses()
{
    ConnectAddress systemAddr(ConnectAddress::StandardBus::System);
    // We'd have to duplicate the ConnectAddress code to check the result in a clean way, so just
    // try to connect instead...
    std::cout << "The system bus address seems to be: " << systemAddr.toString() << '\n';

    EventDispatcher eventDispatcher;
    {
        Connection conn(&eventDispatcher, systemAddr);
        conn.waitForConnectionEstablished();
        TEST(conn.isConnected());
    }

    ConnectAddress sessionAddr(ConnectAddress::StandardBus::Session);
    std::cout << "The session bus address seems to be: " << sessionAddr.toString() << '\n';
    TEST(systemAddr != sessionAddr);

    {
        Connection conn(&eventDispatcher, sessionAddr);
        conn.waitForConnectionEstablished();
        TEST(conn.isConnected());
    }

    // also a few trivial tests of operator==...
    TEST(systemAddr == systemAddr);
    TEST(sessionAddr == sessionAddr);
}

int main(int, char *[])
{
    testFromString();
    testToString();
    testFindBuses();
    std::cout << "Passed!\n";
}
