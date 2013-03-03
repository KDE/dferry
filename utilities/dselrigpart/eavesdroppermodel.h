#ifndef EAVESDROPPERMODEL_H
#define EAVESDROPPERMODEL_H

#include "eavesdropperthread.h"
#include "types.h"

#include <QAbstractItemModel>
#include <QDateTime>

#include <vector>
#include <map>

class Message;
class MessageSortFilter;

struct MessageRecord
{
    MessageRecord(Message *msg, QDateTime time)
       : message(msg),
         otherMessageIndex(-1),
         timestamp(time)
    {}
    QString type() const;
    // the serial of the "conversation", i.e. request-response pair
    uint32 conversationSerial() const;
    // either the method name, or if this is a response the request's method name
    QString conversationMethod(const std::vector<MessageRecord> &container) const;
    QString niceSender(const std::vector<MessageRecord> &container) const;
    uint latency() const; // ## do this here or in some proxy model?

    Message *message;
    int otherMessageIndex;
    QDateTime timestamp;
};

class EavesdropperModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    EavesdropperModel();
    ~EavesdropperModel();

    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    bool hasChildren (const QModelIndex &parent = QModelIndex()) const;
    QModelIndex parent(const QModelIndex &child) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;

private slots:
    void addMessage(Message *message, QDateTime timestamp);

private:
    // for direct access to MessageRecords to speed up filtering
    friend class MessageSortFilter;

    EavesdropperThread m_worker;
    std::map<uint32, uint32> m_callsAwaitingResponse;
    std::vector<MessageRecord> m_messages;
};

#endif // EAVESDROPPERMODEL_H
