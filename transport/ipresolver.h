/*
   Copyright (C) 2023 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef IPRESOLVER_H
#define IPRESOLVER_H

// For platforms with a POSIX-like API but no getaddrinfo(), comment this out
#define USE_GETADDRINFO

#ifdef __unix__
#ifdef USE_GETADDRINFO
#include <netdb.h>
#else
#include <arpa/inet.h>
#endif
#endif

#ifdef _WIN32
#include <ws2tcpip.h>
#endif

class ConnectAddress;

class IpResolver
{
public:
    IpResolver(const ConnectAddress& ca);
    ~IpResolver();

    bool resultValid() const;
    const struct sockaddr* resolved() const;
    socklen_t resolvedLength() const;

private:
#ifdef USE_GETADDRINFO
    struct addrinfo* m_resolved = nullptr;
#else
    struct sockaddr_in m_resolved;
#endif
    bool m_resultValid;
};

#endif // IPRESOLVER_H
