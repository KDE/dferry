/// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef MESSAGESORTFILTER_H
#define MESSAGESORTFILTER_H

#include <QSortFilterProxyModel>

class MessageSortFilter : public QSortFilterProxyModel
{
Q_OBJECT
public:
    MessageSortFilter(QObject *parent = nullptr);
    // reimp
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow (int sourceRow, const QModelIndex &sourceParent) const override;

public slots:
    void setFilterString(const QString &);
    void setOnlyUnanswered(bool);

private:
    QString m_filterString;
    bool m_onlyUnanswered;
};

#endif // MESSAGESORTFILTER_H
