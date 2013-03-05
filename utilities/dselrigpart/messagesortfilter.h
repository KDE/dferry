#ifndef MESSAGESORTFILTER_H
#define MESSAGESORTFILTER_H

#include <QSortFilterProxyModel>

class MessageSortFilter : public QSortFilterProxyModel
{
Q_OBJECT
public:
    MessageSortFilter();
    // reimp
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const;
    bool filterAcceptsRow (int sourceRow, const QModelIndex &sourceParent) const;

public slots:
    void setFilterString(const QString &);

private:
    QString m_filterString;
};

#endif // MESSAGESORTFILTER_H
