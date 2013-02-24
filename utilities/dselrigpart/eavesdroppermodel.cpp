#include "eavesdroppermodel.h"

#include "argumentlist.h"
#include "epolleventdispatcher.h"
#include "itransceiverclient.h"
#include "localsocket.h"
#include "message.h"
#include "transceiver.h"

#include <QSocketNotifier>

#include <vector>

static void fillHelloMessage(Message *hello)
{
    hello->setType(Message::MethodCallMessage);
    hello->setDestination(std::string("org.freedesktop.DBus"));
    hello->setInterface(std::string("org.freedesktop.DBus"));
    hello->setPath(std::string("/org/freedesktop/DBus"));
    hello->setMethod(std::string("Hello"));
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
    std::vector<Message *> messages;

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
        delete messages[i];
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
    int serial = 1;
    d->transceiver->setClient(d);
    {
        Message *hello = new Message(serial++);
        fillHelloMessage(hello);
        d->transceiver->sendAsync(hello);

        static const int messageTypeCount = 4;
        const char *messageType[messageTypeCount] = {
            "signal",
            "method_call",
            "method_return",
            "error"
        };
        for (int i = 0; i < messageTypeCount; i++) {
            Message *spyEnable = new Message(serial++);
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
    d->messages.push_back(message);
    endInsertRows();
}

QVariant EavesdropperModel::data(const QModelIndex &index, int role) const
{
    if (role == Qt::DisplayRole) {
        Q_ASSERT(index.row() < d->messages.size());
        Message *m = d->messages[index.row()];
        switch (index.column()) {
        case 0: {
            const std::string method = m->method();
            return QString::fromUtf8(method.c_str(), method.length());
        }
        case 1: {
            const std::string iface = m->interface();
            return QString::fromUtf8(iface.c_str(), iface.length());
        }
        default:
            break;
        }
    }
    return QVariant();

}

QVariant EavesdropperModel::headerData(int section, Qt::Orientation orientation, int role) const
{
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
    return 1;
}
