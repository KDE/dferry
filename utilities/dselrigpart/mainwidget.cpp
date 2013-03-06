#include "mainwidget.h"

#include "argumentsmodel.h"
#include "eavesdroppermodel.h"
#include "messagesortfilter.h"

MainWidget::MainWidget()
{
    m_ui.setupUi(this);

    //m_model = new EavesdropperModel(this);
    m_model = new EavesdropperModel;
    m_sortFilter = new MessageSortFilter; // TODO parent
    m_sortFilter->setSourceModel(m_model);

    connect(m_ui.filterText, SIGNAL(textChanged(QString)), m_sortFilter, SLOT(setFilterString(QString)));
    connect(m_ui.groupCheckbox, SIGNAL(toggled(bool)), this, SLOT(setGrouping(bool)));
    connect(m_ui.messageList, SIGNAL(clicked(QModelIndex)), this, SLOT(itemClicked(QModelIndex)));

    m_ui.messageList->setModel(m_sortFilter);
    m_ui.messageList->setAlternatingRowColors(true);
    m_ui.messageList->setUniformRowHeights(true);

    m_ui.argumentList->setModel(createArgumentsModel(0));
    m_ui.argumentList->resizeColumnToContents(0);
}

void MainWidget::setGrouping(bool enable)
{
    m_sortFilter->sort(enable ? 0 : -1); // the actual column (if >= 0) is ignored in the proxy model
}

void MainWidget::itemClicked(const QModelIndex &index)
{
    QAbstractItemModel *oldModel = m_ui.argumentList->model();
    const int row = m_sortFilter->mapToSource(index).row();
    m_ui.argumentList->setModel(createArgumentsModel(m_model->m_messages[row].message));
    m_ui.argumentList->expandAll();

    // increase the first column's width if necessary, never shrink it automatically.
    QAbstractItemView *aiv = m_ui.argumentList; // sizeHintForColumn is only protected in the subclass?!
    QHeaderView *headerView = m_ui.argumentList->header();
    headerView->resizeSection(0, qMax(aiv->sizeHintForColumn(0), headerView->sectionSize(0)));
    delete oldModel;
}
