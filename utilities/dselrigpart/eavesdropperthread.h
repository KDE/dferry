#ifndef EAVESDROPPERTHREAD_H
#define EAVESDROPPERTHREAD_H

#include "itransceiverclient.h"

#include <QDateTime>
#include <QThread>

class EavesdropperModel;
class EpollEventDispatcher;
class Message;
class Transceiver;

// This is a separate thread mainly for accurate timestamps. If this was running in the main
// thread, GUI and other processing would delay the calls to messageReceived() and therefore
// QDateTime::currentDateTime().

class EavesdropperThread : public QObject, public ITransceiverClient
{
Q_OBJECT
public:
    EavesdropperThread(EavesdropperModel *model);

    // reimplemented ITransceiverClient method
    void messageReceived(Message *message);

signals:
    void messageReceived(Message *message, QDateTime timestamp);

private slots:
    void run();

private:
    QThread m_thread;

    EpollEventDispatcher *m_dispatcher;
    Transceiver *m_transceiver;
};

#endif // EAVESDROPPERTHREAD_H
