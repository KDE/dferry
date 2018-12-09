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

#ifndef AUTHCLIENT_H
#define AUTHCLIENT_H

#include "itransportlistener.h"

#include "iovaluetypes.h"

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

    void setCompletionListener(ICompletionListener *);

private:
    bool readLine();
    bool isEndOfLine() const;
    void advanceState();

    enum State {
        InitialState,
        ExpectOkState,
        ExpectUnixFdResponseState,
        AuthenticationFailedState,
        AuthenticatedState
    };

    State m_state;
    std::string m_line;
    ICompletionListener *m_completionListener;
};

#endif
