#ifndef AUTHNEGOTIATOR_H
#define AUTHNEGOTIATOR_H

#include "iconnectionclient.h"

#include <string>

// TODO we are currently handling all authentication from here; later this class should only
//      enumerate client and server auth mechanisms and then instantiate and pass control to
//      the right IAuthMechanism implementation (with or without staying around as an intermediate).

class AuthNegotiator : public IConnectionClient
{
public:
    explicit AuthNegotiator(IConnection *connection);

    // reimplemented from IConnectionClient
    virtual void notifyConnectionReadyRead();

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

    IConnection *m_connection;
    State m_state;
    std::string m_line;
};

#endif
