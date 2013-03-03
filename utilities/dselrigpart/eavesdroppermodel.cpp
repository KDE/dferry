#include "eavesdroppermodel.h"

#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

#include <QDateTime>
#include <QSocketNotifier>

#include <vector>

struct MessageRecord
{
    MessageRecord(Message *msg)
       : message(msg),
         timestamp(QDateTime::currentDateTime())
    {}
    QString type() const;
    // the serial of the "conversation", i.e. request-response pair
    uint32 conversationSerial() const;
    uint latency() const; // ## do this here or in some proxy model?

    Message *message;
    QDateTime timestamp;
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

static void fillEavesdropMessage(Message *spyEnable, const char *messageType)
{
    spyEnable->setType(Message::MethodCallMessage);
    spyEnable->setDestination(std::string("org.freedesktop.DBus"));
    spyEnable->setInterface(std::string("org.freedesktop.DBus"));
    spyEnable->setPath(std::string("/org/freedesktop/DBus"));
    spyEnable->setMethod(std::string("AddMatch"));
    ArgumentList argList;
    ArgumentList::WriteCursor writer = argList.beginWrite();
    std::string str = "eavesdrop=true,type=";
    str += messageType;
    writer.writeString(cstring(str.c_str()));
    writer.finish();
    spyEnable->setArgumentList(argList);
}

class EavesdropperModel::Private : public ITransceiverClient
{
public:
    Private(EavesdropperModel *model);
    ~Private();

    // reimplemented ITransceiverClient method
    void messageReceived(Message *message);

    EavesdropperModel *q;
    std::vector<MessageRecord> messages;

    EpollEventDispatcher *dispatcher;
    Transceiver *transceiver;
};

EavesdropperModel::Private::Private(EavesdropperModel *model)
    : q(model)
{
}

EavesdropperModel::Private::~Private()
{
    for (int i = 0; i < messages.size(); i++) {
        delete messages[i].message;
    }
    delete dispatcher;
    delete transceiver;
}

void EavesdropperModel::Private::messageReceived(Message *message)
{
    q->addMessage(message);
}

EavesdropperModel::EavesdropperModel()
   : d(new Private(this))
{
    d->dispatcher = new EpollEventDispatcher;

    d->transceiver = new Transceiver(d->dispatcher);
    d->transceiver->setClient(d);
    {
        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            Message *spyEnable = new Message;
            fillEavesdropMessage(spyEnable, messageType[i]);
            d->transceiver->sendAsync(spyEnable);
        }
    }

    QSocketNotifier *notifier = new QSocketNotifier(d->dispatcher->pollDescriptor(),
                                                    QSocketNotifier::Read, this);
    connect(notifier, SIGNAL(activated(int)), this, SLOT(fileDescriptorReady(int)));
}

void EavesdropperModel::fileDescriptorReady(int fd)
{
    d->dispatcher->poll();
}

EavesdropperModel::~EavesdropperModel()
{
    delete d;
    d = 0;
}

void EavesdropperModel::addMessage(Message *message)
{
    beginInsertRows(QModelIndex(), d->messages.size(), d->messages.size());
    d->messages.push_back(MessageRecord(message));
    endInsertRows();
}

QVariant EavesdropperModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole) {
        Q_ASSERT(index.row() < d->messages.size());
        const MessageRecord &mr = d->messages[index.row()];
        switch (index.column()) {
        case 0:
            return mr.type();
        case 1: {
            const std::string method = mr.message->method();
            return QString::fromUtf8(method.c_str(), method.length());
        }
        case 2: {
            const std::string iface = mr.message->interface();
            return QString::fromUtf8(iface.c_str(), iface.length());
        }
        case 3: {
            const std::string sender = mr.message->sender();
            return QString::fromUtf8(sender.c_str(), sender.length());
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
    return d->messages.size();
}

int EavesdropperModel::columnCount(const QModelIndex &parent) const
{
    return 5;
}
