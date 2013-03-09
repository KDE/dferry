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

#ifndef EAVESDROPPERMODEL_H
#define EAVESDROPPERMODEL_H

#include "eavesdropperthread.h"
#include "types.h"

#include <QAbstractItemModel>

#include <map>
#include <string>
#include <vector>

class MainWidget;
class Message;
class MessageSortFilter;

struct MessageRecord
{
    MessageRecord(Message *msg, qint64 time)
       : message(msg),
         otherMessageIndex(-1),
         timestamp(time)
    {}
    QString type() const;
    // the serial of the "conversation", i.e. request-response pair
    uint32 conversationSerial() const;
    // either the method name, or if this is a response the request's method name
    QString conversationMethod(const std::vector<MessageRecord> &container) const;
    // time unit is nanoseconds
    qint64 conversationStartTime(const std::vector<MessageRecord> &container) const;
    qint64 roundtripTime(const std::vector<MessageRecord> &container) const;
    QString niceSender(const std::vector<MessageRecord> &container) const;
    bool couldHaveNicerDestination() const;
    QString niceDestination(const std::vector<MessageRecord> &container) const;

    Message *message;
    int otherMessageIndex;
    qint64 timestamp;

private:
    bool isReplyToKnownCall() const;
};

class EavesdropperModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    EavesdropperModel(QObject *parent = 0);
    ~EavesdropperModel();

    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    bool hasChildren (const QModelIndex &parent = QModelIndex()) const;
    QModelIndex parent(const QModelIndex &child) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;

private slots:
    void addMessage(Message *message, qint64 timestamp);

private:
    // for access to Message pointers to read arguments
    friend class MainWidget;
    // for direct access to MessageRecords to speed up filtering
    friend class MessageSortFilter;

    EavesdropperThread m_worker;

    struct Call {
        Call(uint32 s, const std::string &e)
           : serial(s), endpoint(e) {}

        bool operator<(const Call &other) const
        {
            if (serial != other.serial) {
                return serial < other.serial;
            }
            return endpoint < other.endpoint;
        }

        uint32 serial;
        std::string endpoint;
    };
    std::map<Call, uint32> m_callsAwaitingResponse; // the value is an index in m_messages
    std::vector<MessageRecord> m_messages;
};

#endif // EAVESDROPPERMODEL_H
