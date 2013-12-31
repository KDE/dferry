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

#include "analyzermainwindow.h"

#include <kaction.h>
#include <kactioncollection.h>
#include <kconfig.h>
#include <kedittoolbar.h>
#include <kfiledialog.h>
#include <kshortcutsdialog.h>
#include <klibloader.h>
#include <kmessagebox.h>
#include <kservice.h>
#include <kstandardaction.h>
#include <kstatusbar.h>
#include <kurl.h>

#include <QApplication>

#include <QDebug>

AnalyzerMainWindow::AnalyzerMainWindow()
   : KParts::MainWindow()
{
 
    // Setup our actions
    setupActions();
 
    //query the .desktop file to load the requested Part
    KService::Ptr service = KService::serviceByDesktopPath("dferanalyzer_part.desktop");
 
    if (service) {
        QString errorMsg;
        m_part = service->createInstance<KParts::ReadOnlyPart>(0, QVariantList(), &errorMsg);
        if (m_part) {
            // tell the KParts::MainWindow that this is indeed
            // the main widget
            setCentralWidget(m_part->widget());
            qDebug() << "central widget is" << m_part->widget();
 
            setupGUI(ToolBar | Keys | StatusBar | Save);
 
            // and integrate the part's GUI with the shell's
            createGUI(m_part);
        } else {
            qDebug() << "service->createInstance() failed:" << errorMsg;
            return;
        }
    } else {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        KMessageBox::error(this, "service dferanalyzer_part.desktop not found");
        qApp->quit();
        // we return here, cause qApp->quit() only means "exit the
        // next time we enter the event loop...
        return;
    }
}
 
AnalyzerMainWindow::~AnalyzerMainWindow()
{
}
 
void AnalyzerMainWindow::load(const KUrl& url)
{
    m_part->openUrl(url);
}
 
void AnalyzerMainWindow::setupActions()
{
    KStandardAction::open(this, SLOT(load()), actionCollection());
    KStandardAction::quit(qApp, SLOT(closeAllWindows()), actionCollection());
}
 
void AnalyzerMainWindow::load()
{
    load(KFileDialog::getOpenUrl());
}
