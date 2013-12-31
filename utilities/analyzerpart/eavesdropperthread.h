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

#ifndef EAVESDROPPERTHREAD_H
#define EAVESDROPPERTHREAD_H

#include "itransceiverclient.h"

#include <QElapsedTimer>
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
    void messageReceived(Message *message, qint64 timestamp);

private slots:
    void run();

private:
    QThread m_thread;
    QElapsedTimer m_timer;
    EpollEventDispatcher *m_dispatcher;
    Transceiver *m_transceiver;
};

#endif // EAVESDROPPERTHREAD_H
