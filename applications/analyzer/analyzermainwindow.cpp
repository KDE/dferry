// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "analyzermainwindow.h"

#include "mainwidget.h"

#include <QFileDialog>
#include <QMenuBar>

AnalyzerMainWindow::AnalyzerMainWindow()
   : QMainWindow()
{
    m_mainWidget = new MainWidget();
    setCentralWidget(m_mainWidget);
    setupActions();
}
 
AnalyzerMainWindow::~AnalyzerMainWindow()
{
}
 
void AnalyzerMainWindow::setupActions()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open, this, &AnalyzerMainWindow::load);
    fileMenu->addAction(tr("&Save As..."), QKeySequence::SaveAs, this, &AnalyzerMainWindow::saveAs);
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, qApp, &QApplication::closeAllWindows);
}
 
void AnalyzerMainWindow::load()
{
    m_mainWidget->load(QFileDialog::getOpenFileName());
}

void AnalyzerMainWindow::saveAs()
{
    m_mainWidget->save(QFileDialog::getSaveFileName());
}
