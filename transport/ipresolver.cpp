// SPDX-FileCopyrightText: 2023 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef RUNNING_DOXYGEN

#include "ipresolver_p.h"

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

#endif // RUNNING_DOXYGEN
