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

#include "ipresolver.h"

#include "connectaddress.h"

#include <cstring>

IpResolver::IpResolver(const ConnectAddress& ca)
{
    std::string hostname = ca.hostname();
    if (hostname.empty() || hostname == "localhost") {
        hostname = "127.0.0.1";
    }

#ifdef USE_GETADDRINFO
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_NUMERICHOST;
    hints.ai_family = AF_INET;

    m_resultValid = getaddrinfo(hostname.c_str(), nullptr, &hints, &m_resolved) == 0;
    if (m_resultValid) {
        // ### Careful with that cast - if we ever support IPv6, casting to sockaddr_in might be unsafe!
        // (Though sin_port is the second field in both, and the first two fields of sockaddr_in and
        // sockaddr_in6 seem to be the same - check if that is guaranteed!)
        auto in_addr = reinterpret_cast<struct sockaddr_in *>(m_resolved->ai_addr);
        in_addr->sin_port = htons(ca.port());
    }
#else
    memset(&m_resolved, 0, sizeof(m_resolved));
    m_resultValid = inet_aton(hostname.c_str(), &m_resolved.sin_addr) == 1;
    // The m_resultValid check is not really necessary, unlike in the getaddrinfo() codepath. We do it
    // to ensure that resolved() returns something unusable with both codepaths, to detect errors.
    if (m_resultValid) {
        m_resolved.sin_family = AF_INET;
        m_resolved.sin_port = htons(ca.port());
    }
#endif
}

IpResolver::~IpResolver()
{
#ifdef USE_GETADDRINFO
    if (m_resultValid) {
        freeaddrinfo(m_resolved);
    }
#endif
}

bool IpResolver::resultValid() const
{
    return m_resultValid;
}

const struct sockaddr* IpResolver::resolved() const
{
#ifdef USE_GETADDRINFO
    return m_resolved->ai_addr;
#else
    return reinterpret_cast<const struct sockaddr *>(&m_resolved);
#endif
}

socklen_t IpResolver::resolvedLength() const
{
#ifdef USE_GETADDRINFO
    return m_resolved->ai_addrlen;
#else
    return sizeof(m_resolved);
#endif
}
