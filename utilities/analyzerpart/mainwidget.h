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

private slots:
    void clear();
    void setGrouping(bool enable);
    void itemClicked(const QModelIndex &index);

private:
    Ui::MainWidget m_ui;
    EavesdropperModel *m_model;
    MessageSortFilter *m_sortFilter;
};

#endif // MAINWIDGET_H
