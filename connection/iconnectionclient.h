#ifndef ICONNECTIONCLIENT_H
#define ICONNECTIONCLIENT_H

class IConnection;

class IConnectionClient
{
public:
    IConnectionClient();
    virtual ~IConnectionClient();

    void setIsReadNotificationEnabled(bool enable);
    bool isReadNotificationEnabled() const;

    void setIsWriteNotificationEnabled(bool enable);
    bool isWriteNotificationEnabled() const;

    // public mainly for testing purposes - only call if you know what you're doing
    // no-op default implementations are provided so you only need to reimplement what you need
    virtual void notifyConnectionReadyRead();
    virtual void notifyConnectionReadyWrite();

protected:
    IConnection *connection() const; // returns m_connection
    bool m_isReadNotificationEnabled;
    bool m_isWriteNotificationEnabled;
    friend class IConnection;
private:
    IConnection *m_connection; // set from IConnection::addClient() / removeClient()
};

#endif // ICONNECTIONCLIENT_H
