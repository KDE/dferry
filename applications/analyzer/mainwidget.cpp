// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "mainwidget.h"

#include "argumentsmodel.h"
#include "eavesdroppermodel.h"
#include "messagesortfilter.h"

MainWidget::MainWidget()
{
    m_ui.setupUi(this);

    m_model = new EavesdropperModel(this);
    m_sortFilter = new MessageSortFilter(m_model);
    m_sortFilter->setSourceModel(m_model);

    connect(m_ui.captureButton, &QAbstractButton::toggled, m_model, &EavesdropperModel::setRecording);
    connect(m_ui.clearButton, &QAbstractButton::clicked, this, &MainWidget::clear);
    connect(m_ui.filterText, &QLineEdit::textChanged, m_sortFilter, &MessageSortFilter::setFilterString);
    connect(m_ui.unansweredCheckbox, &QCheckBox::toggled, m_sortFilter, &MessageSortFilter::setOnlyUnanswered);
    connect(m_ui.groupCheckbox, &QCheckBox::toggled, this, &MainWidget::setGrouping);
    connect(m_ui.messageList, &QAbstractItemView::clicked, this, &MainWidget::itemClicked);

    m_ui.messageList->setModel(m_sortFilter);
    m_ui.messageList->setAlternatingRowColors(true);
    m_ui.messageList->setUniformRowHeights(true);

    m_ui.arguments->setModel(createArgumentsModel(nullptr));
    m_ui.arguments->resizeColumnToContents(0);

    // TODO: when a new item appears (what to do if it comes from filter changes?), and the list view scroll
    //       position is at the bottom, keep it scrolled to the bottom (that is scroll after appending).
    //       It looks about like this in Konversation: if (wasAtBottom) m_bar->setValue(m_bar->maximum());
}

void MainWidget::clear()
{
    m_ui.arguments->setModel(createArgumentsModel(nullptr));
    m_model->clear();
}

void MainWidget::setGrouping(bool enable)
{
    m_sortFilter->sort(enable ? 0 : -1); // the actual column (if >= 0) is ignored in the proxy model
}

void MainWidget::itemClicked(const QModelIndex &index)
{
    QAbstractItemModel *oldModel = m_ui.arguments->model();
    const int row = m_sortFilter->mapToSource(index).row();
    m_ui.arguments->setModel(createArgumentsModel(m_model->m_messages[row].message));
    m_ui.arguments->expandAll();

    // increase the first column's width if necessary, never shrink it automatically.
    QAbstractItemView *aiv = m_ui.arguments; // sizeHintForColumn is only protected in the subclass?!
    QHeaderView *headerView = m_ui.arguments->header();
    headerView->resizeSection(0, qMax(aiv->sizeHintForColumn(0), headerView->sectionSize(0)));
    delete oldModel;
}

void MainWidget::load(const QString &filePath)
{
    m_model->loadFromFile(filePath);
}

void MainWidget::save(const QString &filePath)
{
    m_model->saveToFile(filePath);
}
