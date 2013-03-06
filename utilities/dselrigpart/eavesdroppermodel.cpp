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

#include "eavesdroppermodel.h"

#include "argumentlist.h"
#include "message.h"

enum {
    TypeColumn = 0,
    MethodColumn,
    InterfaceColumn,
    PathColumn,
    SenderColumn,
    DestinationColumn,
    ColumnCount
};

QString MessageRecord::type() const
{
    switch (message->type()) {
    case Message::MethodCallMessage:
        return QObject::tr("Call");
    case Message::MethodReturnMessage:
        return QObject::tr("Return");
    case Message::ErrorMessage:
        return QObject::tr("Error");
    case Message::SignalMessage:
        return QObject::tr("Signal");
    case Message::InvalidMessage:
        return QString::fromLatin1("???");
    }
    Q_ASSERT(false);
    return QString();
}

uint32 MessageRecord::conversationSerial() const
{
    if (message->type() == Message::MethodReturnMessage || message->type() == Message::ErrorMessage) {
        return message->replySerial();
    }
    return message->serial();
}

QString MessageRecord::conversationMethod(const std::vector<MessageRecord> &container) const
{
    std::string method;
    if (message->type() == Message::MethodReturnMessage) {
        if (otherMessageIndex >= 0) {
            method = container[otherMessageIndex].message->method();
        }
    } else {
         method = message->method();
    }
    return QString::fromStdString(method);
}

QDateTime MessageRecord::conversationStartTime(const std::vector<MessageRecord> &container) const
{
    if (message->type() == Message::MethodReturnMessage && otherMessageIndex >= 0) {
        return container[otherMessageIndex].timestamp;
    }
    return timestamp;
}

QString MessageRecord::niceSender(const std::vector<MessageRecord> &container) const
{
    std::string sender = message->sender();
    if (message->type() == Message::MethodReturnMessage && otherMessageIndex >= 0) {
        sender += " (";
        sender += container[otherMessageIndex].message->destination();
        sender += ')';
    }
    return QString::fromStdString(sender);
}

bool MessageRecord::couldHaveNicerDestination() const
{
    if (message->type() != Message::MethodCallMessage || otherMessageIndex < 0) {
        return false;
    }
    const std::string dest = message->destination();
    return !dest.empty() && dest[0] != ':';
}

QString MessageRecord::niceDestination(const std::vector<MessageRecord> &container) const
{
    std::string dest = message->destination();
    if (couldHaveNicerDestination()) {
        dest += " (";
        dest += container[otherMessageIndex].message->sender();
        dest += ')';
    }
    return QString::fromStdString(dest);
}

EavesdropperModel::EavesdropperModel(QObject *parent)
   : QAbstractItemModel(parent),
     m_worker(this)
{
}

EavesdropperModel::~EavesdropperModel()
{
    for (int i = 0; i < m_messages.size(); i++) {
        delete m_messages[i].message;
    }
}

void EavesdropperModel::addMessage(Message *message, QDateTime timestamp)
{
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.push_back(MessageRecord(message, timestamp));

    const uint currentMessageIndex = m_messages.size() - 1;

    // Connect responses with previously spotted calls because information from one is useful for the other.
    // We must match the call sender with the reply receiver, instead of the call receiver with the reply
    // sender, because calls can go to well-known addresses that are only resolved to a concrete endpoint
    // by the bus daemon.

    if (message->type() == Message::MethodCallMessage) {
        // the NO_REPLY_EXPECTED flag does *not* forbid a reply, so we disregard the flag
        // ### it would be nice to clean up m_callsAwaitingResponse periodically, but we allocate
        //     memory that is not freed before shutdown left and right so it doesn't make much of
        //     a difference. it does make a difference when serials overflow.
        m_callsAwaitingResponse[Call(message->serial(), message->sender())] = currentMessageIndex;
    } else if (message->type() == Message::MethodReturnMessage || message->type() == Message::ErrorMessage) {
        Call key(message->replySerial(), message->destination());
        std::map<Call, uint32>::iterator it = m_callsAwaitingResponse.find(key);
        // we could have missed the initial call because it happened before we connected to the bus...
        // theoretically we could assert the presence of the call after one d-bus timeout has passed
        if (it != m_callsAwaitingResponse.end()) {
            const uint originalMessageIndex = it->second;
            m_messages.back().otherMessageIndex = originalMessageIndex;
            m_messages[originalMessageIndex].otherMessageIndex = currentMessageIndex;
            m_callsAwaitingResponse.erase(it);
            if (m_messages[originalMessageIndex].couldHaveNicerDestination()) {
                const QModelIndex index = createIndex(originalMessageIndex, DestinationColumn);
                emit dataChanged(index, index);
            }
        }
    }
    endInsertRows();
}

QVariant EavesdropperModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole) {
        Q_ASSERT(index.row() < m_messages.size());
        const MessageRecord &mr = m_messages[index.row()];
        switch (index.column()) {
        case TypeColumn:
            return mr.type();
        case MethodColumn:
            return mr.conversationMethod(m_messages);
        case InterfaceColumn:
            return QString::fromStdString(mr.message->interface());
        case PathColumn:
            return QString::fromStdString(mr.message->path());
        case SenderColumn:
            return mr.niceSender(m_messages);
        case DestinationColumn:
            return mr.niceDestination(m_messages);
        default:
            break;
        }
    }
    return QVariant();
}

QVariant EavesdropperModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole && orientation == Qt::Horizontal) {
        switch (section) {
        case TypeColumn:
            return tr("Type");
        case MethodColumn:
            return tr("Method");
        case InterfaceColumn:
            return tr("Interface");
        case PathColumn:
            return tr("Path");
        case SenderColumn:
            return tr("Sender");
        case DestinationColumn:
            return tr("Destination");
        default:
            break;
        }
    }
    return QVariant();
}

QModelIndex EavesdropperModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid()) {
        Q_ASSERT(parent.model() == this);
        return QModelIndex();
    }
    return createIndex(row, column);
}

bool EavesdropperModel::hasChildren (const QModelIndex &parent) const
{
    return !parent.isValid();
}

QModelIndex EavesdropperModel::parent(const QModelIndex &child) const
{
    Q_ASSERT(!child.isValid() || child.model() == this); // ### why does it crash with &&?
    return QModelIndex();
}

int EavesdropperModel::rowCount(const QModelIndex &parent) const
{
    return m_messages.size();
}

int EavesdropperModel::columnCount(const QModelIndex &parent) const
{
    return ColumnCount;
}
