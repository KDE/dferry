#ifndef AUTHNEGOTIATOR_H
#define AUTHNEGOTIATOR_H

#include "iconnectionclient.h"

#include "types.h"

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

    };

    IConnection *m_connection;
    State m_state;
    static const int m_maxLineLength = 256;
    byte m_lineBuffer[m_maxLineLength];
    cstring m_line;
};

#endif
