#include "mainwidget.h"

#include "eavesdroppermodel.h"
#include "messagesortfilter.h"

#include <QLineEdit>
#include <QTreeView>
#include <QVBoxLayout>

MainWidget::MainWidget()
{
    m_ui.setupUi(this);

    //EavesdropperModel *model = new EavesdropperModel(this);
    EavesdropperModel *model = new EavesdropperModel;
    m_sortFilter = new MessageSortFilter; // TODO parent
    m_sortFilter->setSourceModel(model);

    connect(m_ui.filterText, SIGNAL(textChanged(QString)), m_sortFilter, SLOT(setFilterString(QString)));
    connect(m_ui.groupCheckbox, SIGNAL(toggled(bool)), this, SLOT(setGrouping(bool)));

    m_ui.messageList->setModel(m_sortFilter);
    m_ui.messageList->setAlternatingRowColors(true);
}

void MainWidget::setGrouping(bool enable)
{
    m_sortFilter->sort(enable ? 0 : -1); // the actual column (if >= 0) is ignored in the proxy model
}
