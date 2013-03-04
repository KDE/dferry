#include "eavesdroppermodel.h"

#include "argumentlist.h"
#include "message.h"

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
    return QString::fromUtf8(method.c_str(), method.length());
}

QString MessageRecord::niceSender(const std::vector<MessageRecord> &container) const
{
    std::string sender = message->sender();
    if (message->type() == Message::MethodReturnMessage && otherMessageIndex >= 0) {
        sender += " (";
        sender += container[otherMessageIndex].message->destination();
        sender += ')';
    }
    return QString::fromUtf8(sender.c_str(), sender.length());
}

EavesdropperModel::EavesdropperModel()
   : m_worker(this)
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

    if (message->type() == Message::MethodCallMessage) {
        // the NO_REPLY_EXPECTED flag does *not* forbid a reply, so we disregard the flag
        // ### it would be nice to clean up m_callsAwaitingResponse periodically, but we allocate
        //     memory that is not freed before shutdown left and right so it doesn't make much of
        //     a difference
        m_callsAwaitingResponse[message->serial()] = currentMessageIndex;
    } else if (message->type() == Message::MethodReturnMessage || message->type() == Message::ErrorMessage) {
        std::map<uint32, uint32>::iterator it = m_callsAwaitingResponse.find(message->replySerial());
        // we could have missed the initial call because it happened before we connected to the bus...
        // theoretically we could assert the presence of the call after one d-bus timeout has passed
        if (it != m_callsAwaitingResponse.end()) {
            const uint originalMessageIndex = it->second;
            m_messages.back().otherMessageIndex = originalMessageIndex;
            m_messages[originalMessageIndex].otherMessageIndex = currentMessageIndex;
            m_callsAwaitingResponse.erase(it);
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
        case 0:
            return mr.type();
        case 1: {
            return mr.conversationMethod(m_messages);
        }
        case 2: {
            const std::string iface = mr.message->interface();
            return QString::fromUtf8(iface.c_str(), iface.length());
        }
        case 3: {
            return mr.niceSender(m_messages);
        }
        case 4: {
            const std::string destination = mr.message->destination();
            return QString::fromUtf8(destination.c_str(), destination.length());
        }
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
        case 0:
            return tr("Type");
        case 1:
            return tr("Method");
        case 2:
            return tr("Interface");
        case 3:
            return tr("Sender");
        case 4:
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
    return 5;
}
