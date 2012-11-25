#ifndef ICONNECTIONCLIENT_H
#define ICONNECTIONCLIENT_H

class IConnection;

class IConnectionClient
{
public:
    // no-op default implementations are provided so you only need to reimplement what you need
    virtual ~IConnectionClient();
    virtual void notifyConnectionReadyRead();
    virtual void notifyConnectionReadyWrite();

    // TODO setConnection(IConnection *connection) const;
    // TODO IConnection *connection() const;

private:
    friend class IConnection;
    // TODO IConnection *m_connection;
};

#endif // ICONNECTIONCLIENT_H
