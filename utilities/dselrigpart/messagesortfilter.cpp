#include "messagesortfilter.h"
#include "eavesdroppermodel.h"

#include "message.h"

MessageSortFilter::MessageSortFilter()
{
    setDynamicSortFilter(true);
}

bool MessageSortFilter::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    if (m_filterString.isEmpty() || sourceParent.isValid()) {
        return true;
    }
    EavesdropperModel *model = static_cast<EavesdropperModel *>(sourceModel());

    const std::vector<MessageRecord> &msgList = model->m_messages;
    const MessageRecord &msg = msgList[sourceRow];
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
