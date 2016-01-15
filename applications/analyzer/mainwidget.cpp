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

    connect(m_ui.captureButton, SIGNAL(toggled(bool)), m_model, SLOT(setRecording(bool)));
    connect(m_ui.clearButton, SIGNAL(clicked()), this, SLOT(clear()));
    connect(m_ui.filterText, SIGNAL(textChanged(QString)), m_sortFilter, SLOT(setFilterString(QString)));
    connect(m_ui.unansweredCheckbox, SIGNAL(toggled(bool)), m_sortFilter, SLOT(setOnlyUnanswered(bool)));
    connect(m_ui.groupCheckbox, SIGNAL(toggled(bool)), this, SLOT(setGrouping(bool)));
    connect(m_ui.messageList, SIGNAL(clicked(QModelIndex)), this, SLOT(itemClicked(QModelIndex)));

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
    m_ui.arguments->setModel(createArgumentsModel(0));
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
