// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef AUTHCLIENT_P_H
#define AUTHCLIENT_P_H

#include "itransportlistener_p.h"

#include "iovaluetypes_p.h"

#include <string>

// TODO we are currently handling all authentication from here; later this class should only
//      enumerate client and server auth mechanisms and then instantiate and pass control to
//      the right IAuthMechanism implementation (with or without staying around as an intermediate).

class ICompletionListener;

class AuthClient : public ITransportListener
{
public:
    explicit AuthClient(ITransport *transport);

    // reimplemented from ITransportListener
    IO::Status handleTransportCanRead() override;

    bool isFinished() const;
    bool isAuthenticated() const;
    bool isUnixFdPassingEnabled() const;

    void setCompletionListener(ICompletionListener *);

private:
    bool readLine();
    bool isEndOfLine() const;
    void sendNextAuthMethod();
    void advanceState();

    enum State {
        InitialState,
        ExpectOkState,
        ExpectUnixFdResponseState,
        AuthenticationFailedState,
        AuthenticatedState
    };

    enum AuthMethods {
        AuthExternal,
        AuthAnonymous,
        LastAuthMethod // keep this last!
    };

    State m_state;
    int m_nextAuthMethod;
    bool m_fdPassingEnabled;
    std::string m_line;
    ICompletionListener *m_completionListener;
};

#endif
