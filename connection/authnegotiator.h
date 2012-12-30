#ifndef AUTHNEGOTIATOR_H
#define AUTHNEGOTIATOR_H

#include "iconnectionclient.h"

#include <string>

// TODO we are currently handling all authentication from here; later this class should only
//      enumerate client and server auth mechanisms and then instantiate and pass control to
//      the right IAuthMechanism implementation (with or without staying around as an intermediate).

class ICompletionClient;

class AuthNegotiator : public IConnectionClient
{
public:
    explicit AuthNegotiator(IConnection *connection);

    // reimplemented from IConnectionClient
    virtual void notifyConnectionReadyRead();

    bool isFinished() const;
    bool isAuthenticated() const;

    void setCompletionClient(ICompletionClient *);

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
    ICompletionClient *m_completionClient;
};

#endif
