// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "messagesortfilter.h"
#include "eavesdroppermodel.h"

#include "message.h"

#include <QDebug>

MessageSortFilter::MessageSortFilter(QObject *parent)
   : QSortFilterProxyModel(parent),
     m_onlyUnanswered(false)
{
    setDynamicSortFilter(true);
}

bool MessageSortFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (sourceParent.isValid() || (m_filterString.isEmpty() && !m_onlyUnanswered)) {
        return true;
    }

    EavesdropperModel *model = static_cast<EavesdropperModel *>(sourceModel());
    const std::vector<MessageRecord> &msgList = model->m_messages;
    const MessageRecord &msg = msgList[sourceRow];

    if (m_onlyUnanswered) {
        if (msg.message->type() == Message::MethodCallMessage) {
            if (!msg.isAwaitingReply()) {
                return false;
            }
        } else if (msg.message->type() == Message::ErrorMessage) {
            if (!msg.isReplyToKnownCall()) {
                return false;
            }
        } else {
            return false;
        }
    }

    if (m_filterString.isEmpty()) {
        return true;
    }
    return msg.conversationMethod(msgList).contains(m_filterString, Qt::CaseInsensitive) ||
           msg.niceSender(msgList).contains(m_filterString, Qt::CaseInsensitive) ||
           msg.niceDestination(msgList).contains(m_filterString, Qt::CaseInsensitive) ||
           QString::fromStdString(msg.message->interface()).contains(m_filterString, Qt::CaseInsensitive) ||
           QString::fromStdString(msg.message->path()).contains(m_filterString, Qt::CaseInsensitive);
}

bool MessageSortFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    Q_ASSERT(!left.parent().isValid());
    Q_ASSERT(!right.parent().isValid());
    EavesdropperModel *model = static_cast<EavesdropperModel *>(sourceModel());
    const std::vector<MessageRecord> &msgList = model->m_messages;
    const MessageRecord &leftMsg = msgList[left.row()];
    const MessageRecord &rightMsg = msgList[right.row()];

    return leftMsg.conversationStartTime(msgList) < rightMsg.conversationStartTime(msgList);
}

void MessageSortFilter::setFilterString(const QString &s)
{
#if QT_VERSION > QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif

    m_filterString = s;

#if QT_VERSION > QT_VERSION_CHECK(6, 9, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}

void MessageSortFilter::setOnlyUnanswered(bool onlyUnanswered)
{
#if QT_VERSION > QT_VERSION_CHECK(6, 9, 0)
    beginFilterChange();
#endif

    m_onlyUnanswered = onlyUnanswered;

#if QT_VERSION > QT_VERSION_CHECK(6, 9, 0)
    endFilterChange(Direction::Rows);
#else
    invalidateFilter();
#endif
}
