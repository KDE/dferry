// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef EAVESDROPPERTHREAD_H
#define EAVESDROPPERTHREAD_H

#include "imessagereceiver.h"

#include <QElapsedTimer>
#include <QThread>

class EavesdropperModel;
class EventDispatcher;
class Message;
class Connection;

// This is a separate thread mainly for accurate timestamps. If this was running in the main
// thread, GUI and other processing would delay the calls to messageReceived() and therefore
// QDateTime::currentDateTime().

class EavesdropperThread : public QObject, public IMessageReceiver
{
Q_OBJECT
public:
    EavesdropperThread(EavesdropperModel *model);
    ~EavesdropperThread() override;

    // reimplemented IMessageReceiver method
    void handleSpontaneousMessageReceived(Message message, Connection *) override;

signals:
    void messageReceived(Message *message, qint64 timestamp);

private:
    void run();

    QThread m_thread;
    QElapsedTimer m_timer;
    EventDispatcher *m_dispatcher;
    Connection *m_connection;
};

#endif // EAVESDROPPERTHREAD_H
