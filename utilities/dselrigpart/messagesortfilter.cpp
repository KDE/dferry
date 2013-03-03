#include "messagesortfilter.h"

// TODO actually implement those columns and define the constanst over there

static const int ConversationSerialColumn = 10;

MessageSortFilter::MessageSortFilter()
{
    setDynamicSortFilter(true);
}

bool MessageSortFilter::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    if (m_isGroupingConversations && !left.parent().isValid()) {
        Q_ASSERT(!right.parent().isValid());
        QVariant leftConvoSerial = left.sibling(left.row(), ConversationSerialColumn).data();
        QVariant rightConvoSerial = right.sibling(right.row(), ConversationSerialColumn).data();
        return leftConvoSerial.toUInt() < rightConvoSerial.toUInt();
    }
    // we assume that the only indices we get are those of the active sorting column
    Q_ASSERT(left.column() == right.column());
    switch (left.column()) {
    case ConversationSerialColumn:
        break;
    }

    return false; // HACK
}
