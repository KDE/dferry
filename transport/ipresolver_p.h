// SPDX-FileCopyrightText: 2023 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IPRESOLVER_P_H
#define IPRESOLVER_P_H

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

#endif // IPRESOLVER_P_H
