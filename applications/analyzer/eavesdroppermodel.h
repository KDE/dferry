// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

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
    MessageRecord(Message *msg, qint64 time);
    MessageRecord();

    QString type() const;
    // whether this is a call that should get a reply, with no reply
    bool isAwaitingReply() const;
    // whether this is a reply that we've seen the call for
    bool isReplyToKnownCall() const;
    // the serial of the "conversation", i.e. request-response pair
    uint32 conversationSerial() const;
    // either the method name, or if this is a response the request's method name
    QString conversationMethod(const std::vector<MessageRecord> &container) const;
    // time unit is nanoseconds
    qint64 conversationStartTime(const std::vector<MessageRecord> &container) const;
    qint64 roundtripTime(const std::vector<MessageRecord> &container) const;
    QString niceSender(const std::vector<MessageRecord> &container) const;
    bool couldHaveNicerDestination(const std::vector<MessageRecord> &container) const;
    QString niceDestination(const std::vector<MessageRecord> &container) const;

    Message *message;
    int otherMessageIndex;
    qint64 timestamp;
};

class EavesdropperModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    EavesdropperModel(QObject *parent = nullptr);
    ~EavesdropperModel() override;

    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    bool hasChildren (const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;

public slots:
    void setRecording(bool recording);
    void clear();
    void saveToFile(const QString &path);
    bool loadFromFile(const QString &path);

private:
    friend class EavesdropperThread;
    void addMessage(Message *message, qint64 timestamp);

    void clearInternal();

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

    bool m_isRecording;
    std::map<Call, uint32> m_callsAwaitingResponse; // the value is an index in m_messages
    std::vector<MessageRecord> m_messages;
};

#endif // EAVESDROPPERMODEL_H
