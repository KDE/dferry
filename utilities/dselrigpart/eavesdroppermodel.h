#ifndef EAVESDROPPERMODEL_H
#define EAVESDROPPERMODEL_H

#include <QAbstractItemModel>

class Message;

class EavesdropperModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    EavesdropperModel();
    ~EavesdropperModel();

    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const;
    QModelIndex parent(const QModelIndex &child) const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    int columnCount(const QModelIndex &parent = QModelIndex()) const;

private:
    void addMessage(Message *message);

    class Private;
    Private *d;
};

#endif // EAVESDROPPERMODEL_H
