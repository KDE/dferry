#include "rigmainwindow.h"

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

RigMainWindow::RigMainWindow()
   : KParts::MainWindow()
{
 
    // Setup our actions
    setupActions();
 
    //query the .desktop file to load the requested Part
    KService::Ptr service = KService::serviceByDesktopPath("dselrig_part.desktop");
 
    if (service) {
        m_part = service->createInstance<KParts::ReadOnlyPart>(0);
        if (m_part) {
            // tell the KParts::MainWindow that this is indeed
            // the main widget
            setCentralWidget(m_part->widget());
            qDebug() << "central widget is" << m_part->widget();
 
            setupGUI(ToolBar | Keys | StatusBar | Save);
 
            // and integrate the part's GUI with the shell's
            createGUI(m_part);
        } else {
            qDebug() << "service->createInstance() failed!";
            return;
        }
    } else {
        // if we couldn't find our Part, we exit since the Shell by
        // itself can't do anything useful
        KMessageBox::error(this, "service dselrig_part.desktop not found");
        qApp->quit();
        // we return here, cause qApp->quit() only means "exit the
        // next time we enter the event loop...
        return;
    }
}
 
RigMainWindow::~RigMainWindow()
{
}
 
void RigMainWindow::load(const KUrl& url)
{
    m_part->openUrl(url);
}
 
void RigMainWindow::setupActions()
{
    KStandardAction::open(this, SLOT(load()), actionCollection());
    KStandardAction::quit(qApp, SLOT(closeAllWindows()), actionCollection());
}
 
void RigMainWindow::load()
{
    load(KFileDialog::getOpenUrl());
}
