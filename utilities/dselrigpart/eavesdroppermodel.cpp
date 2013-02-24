#include "eavesdroppermodel.h"

#include "message.h"
#include "itransceiverclient.h"

#include <vector>

class EavesdropperModel::Private : public ITransceiverClient
{
public:
    Private(EavesdropperModel *model);
    ~Private();

    // reimplemented ITransceiverClient method
    void messageReceived(Message *message);

    EavesdropperModel *q;
    std::vector<Message *> messages;
};

EavesdropperModel::Private::Private(EavesdropperModel *model)
    : q(model)
{}

EavesdropperModel::Private::~Private()
{
    for (int i = 0; i < messages.size(); i++) {
        delete messages[i];
    }
}

void EavesdropperModel::Private::messageReceived(Message *message)
{
    q->addMessage(message);
}

EavesdropperModel::EavesdropperModel()
   : d(new Private(this))
{
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
