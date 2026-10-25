// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef MAINWIDGET_H
#define MAINWIDGET_H

#include <QWidget>

#include "ui_mainwidget.h"

class EavesdropperModel;
class MessageSortFilter;
class QModelIndex;

class MainWidget : public QWidget
{
Q_OBJECT
public:
    MainWidget();

    void load(const QString &filePath);
    void save(const QString &filePath);

private:
    void clear();
    void setGrouping(bool enable);
    void itemClicked(const QModelIndex &index);

    Ui::MainWidget m_ui;
    EavesdropperModel *m_model;
    MessageSortFilter *m_sortFilter;
};

#endif // MAINWIDGET_H
