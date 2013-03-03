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

    bool isGroupingConversations() const;

private:
    bool m_isGroupingConversations;
};

#endif // MESSAGESORTFILTER_H
