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

#include "arguments.h"
#include "message.h"

#include <QDataStream>
#include <QFile>

enum {
    TypeColumn = 0,
    RoundtripTimeColumn,
    MethodColumn,
    InterfaceColumn,
    PathColumn,
    SenderColumn,
    DestinationColumn,
    ColumnCount
};

MessageRecord::MessageRecord(Message *msg, qint64 time)
   : message(msg),
     otherMessageIndex(-1),
     timestamp(time)
{}

MessageRecord::MessageRecord()
   : message(0),
     otherMessageIndex(-1),
     timestamp(-1)
{}

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

bool MessageRecord::isAwaitingReply() const
{
    return message->type() == Message::MethodCallMessage &&
           message->expectsReply() && otherMessageIndex < 0;
}

bool MessageRecord::isReplyToKnownCall() const
{
    return otherMessageIndex >= 0 && (message->type() == Message::MethodReturnMessage ||
                                      message->type() == Message::ErrorMessage);
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
    if (isReplyToKnownCall()) {
        method = container[otherMessageIndex].message->method();
    } else {
        method = message->method();
    }
    return QString::fromStdString(method);
}

qint64 MessageRecord::conversationStartTime(const std::vector<MessageRecord> &container) const
{
    if (isReplyToKnownCall()) {
        return container[otherMessageIndex].timestamp;
    }
    return timestamp;
}

qint64 MessageRecord::roundtripTime(const std::vector<MessageRecord> &container) const
{
    if (isReplyToKnownCall()) {
        return timestamp - container[otherMessageIndex].timestamp;
    }
    return -1;
}

QString MessageRecord::niceSender(const std::vector<MessageRecord> &container) const
{
    // this does something like ":1.2" -> ":1.2 (org.freedesktop.fooInterface)"
    std::string sender = message->sender();
    if (isReplyToKnownCall()) {
        const std::string otherDest = container[otherMessageIndex].message->destination();
        if (!otherDest.empty() && otherDest[0] != ':') {
            sender += " (";
            sender += otherDest;
            sender += ')';
        }
    }
    return QString::fromStdString(sender);
}

bool MessageRecord::couldHaveNicerDestination(const std::vector<MessageRecord> &container) const
{
    // see niceDestination; this returns true if the "raw" destination is *not* of the :n.m type
    // and the other (i.e. reply) message's sender *is* o the :n.m type
    if (message->type() != Message::MethodCallMessage || otherMessageIndex < 0) {
        return false;
    }
    const std::string dest = message->destination();
    if (!dest.empty() && dest[0] == ':') {
        return false;
    }
    const std::string otherSender = container[otherMessageIndex].message->sender();
    return !otherSender.empty() && otherSender[0] == ':';
}

QString MessageRecord::niceDestination(const std::vector<MessageRecord> &container) const
{
    // this does something like "org.freedesktop.fooInterface" -> "org.freedesktop.fooInterface (:1.2)"
    std::string dest = message->destination();
    if (couldHaveNicerDestination(container)) {
        dest += " (";
        dest += container[otherMessageIndex].message->sender();
        dest += ')';
    }
    return QString::fromStdString(dest);
}

EavesdropperModel::EavesdropperModel(QObject *parent)
   : QAbstractItemModel(parent),
     m_worker(this),
     m_isRecording(true)
{
}

EavesdropperModel::~EavesdropperModel()
{
    clearInternal();
}

void EavesdropperModel::addMessage(Message *message, qint64 timestamp)
{
    if (!m_isRecording) {
        return;
    }
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
            if (m_messages[originalMessageIndex].couldHaveNicerDestination(m_messages)) {
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
        case RoundtripTimeColumn: {
            qint64 rtt = mr.roundtripTime(m_messages);
            Q_ASSERT(rtt >= -1); // QElapsedTimer should give us monotonic time
            if (rtt == -1) {
                break;  // no data for a message that doesn't or can't have a reply
            }
            return static_cast<double>(rtt) / 1000000.0; // nsecs / 1E6 -> milliseconds
        }
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
        case RoundtripTimeColumn:
            return tr("Latency [ms]");
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
    Q_UNUSED(child);
    return QModelIndex();
}

int EavesdropperModel::rowCount(const QModelIndex &/* parent */) const
{
    return m_messages.size();
}

int EavesdropperModel::columnCount(const QModelIndex &/* parent */) const
{
    return ColumnCount;
}

void EavesdropperModel::setRecording(bool recording)
{
    // We could stop the eavesdropper thread when not recording, but it doesn't seem worth the effort.
    m_isRecording = recording;
}

void EavesdropperModel::clear()
{
    beginResetModel();
    clearInternal();
    endResetModel();
}

void EavesdropperModel::clearInternal()
{
    m_callsAwaitingResponse.clear();
    // This is easier than making MessageRecord clean up after itself - it would need refcounting in order
    // to avoid accidentally deleting the message in many common situations.
    for (MessageRecord &msgRecord : m_messages) {
        delete msgRecord.message;
        msgRecord.message = nullptr;
    }
    m_messages.clear();
}

static const char *fileHeader = "Dferry binary DBus dump v0001";

void EavesdropperModel::saveToFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(fileHeader);

    for (MessageRecord &msgRecord : m_messages) {
        std::vector<byte> msgData = msgRecord.message->save();

        // auxiliary data from MessageRecord, length prefix
        {
            QDataStream auxStream(&file);
            auxStream.setVersion(12);
            auxStream << msgRecord.otherMessageIndex;
            auxStream << msgRecord.timestamp;
            auxStream << quint32(msgData.size());
        }

        // serialized message just like it would appear on the bus
        file.write(reinterpret_cast<const char*>(&msgData[0]), msgData.size());
    }
}

bool EavesdropperModel::loadFromFile(const QString &path)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    if (file.read(strlen(fileHeader)) != QByteArray(fileHeader)) {
        return false;
    }

    std::vector<MessageRecord> loadedRecords;
    while (!file.atEnd()) {
        MessageRecord record;
        quint32 messageDataSize;
        // auxiliary data from MessageRecord, length prefix
        {
            QDataStream auxStream(&file);
            auxStream.setVersion(12);
            auxStream >> record.otherMessageIndex;
            auxStream >> record.timestamp;
            auxStream >> messageDataSize;
        }
        if (file.atEnd()) {
            return false;
        }

        // serialized message just like it would appear on the bus
        std::vector<byte> msgData(messageDataSize, 0);
        if (file.read(reinterpret_cast<char*>(&msgData[0]), messageDataSize) != messageDataSize) {
            return false;
        }
        record.message = new Message();
        record.message->load(msgData);
        loadedRecords.push_back(record);
    }

    beginResetModel();
    clearInternal();
    m_messages = loadedRecords;
    endResetModel();
    // TODO disable capture or make sure that our call-reply matching features work in the
    //      presence of data loaded from a different session...
    return true;
}
