#ifndef EAVESDROPPERMODEL_H
#define EAVESDROPPERMODEL_H

#include "eavesdropperthread.h"
#include "types.h"

#include <QAbstractItemModel>
#include <QDateTime>

#include <map>
#include <string>
#include <vector>

class MainWidget;
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
    QDateTime conversationStartTime(const std::vector<MessageRecord> &container) const;
    QString niceSender(const std::vector<MessageRecord> &container) const;
    bool couldHaveNicerDestination() const;
    QString niceDestination(const std::vector<MessageRecord> &container) const;
    uint latency() const; // TODO which time units? - also consider moving away from QDateTime altogether

    Message *message;
    int otherMessageIndex;
    QDateTime timestamp;
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
    void addMessage(Message *message, QDateTime timestamp);

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
