/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LGPL.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.

   Alternatively, this file is available under the Mozilla Public License
   Version 1.1.  You may obtain a copy of the License at
   http://www.mozilla.org/MPL/
*/

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
    m_filterString = s;
    invalidateFilter();
}

void MessageSortFilter::setOnlyUnanswered(bool onlyUnanswered)
{
    m_onlyUnanswered = onlyUnanswered;
    invalidateFilter();
}
