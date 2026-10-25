/// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ANALYZERMAINWINDOW_H
#define ANALYZERMAINWINDOW_H
 
#include <QMainWindow>
 
class MainWidget;

class AnalyzerMainWindow : public QMainWindow
{
    Q_OBJECT
public:
    AnalyzerMainWindow();
    ~AnalyzerMainWindow() override;
 
public slots:
    void load();
    void saveAs();
 
private:
    void setupActions();
 
    MainWidget *m_mainWidget;
};
 
#endif // ANALYZERMAINWINDOW_H
