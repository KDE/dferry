#include <KApplication>
#include <KAboutData>
#include <KCmdLineArgs>
#include <KUrl>

#include "rigmainwindow.h"

int main (int argc, char *argv[])
{
    KAboutData aboutData("dselrig", "dselrig", ki18n("DselRig"), "0.4",
                         ki18n("A MainWindow for a DselRigPart."),
                         KAboutData::License_GPL,
                         ki18n("Copyright 2013 Andreas Hartmetz"));
    KCmdLineArgs::init(argc, argv, &aboutData);

    KApplication app;

    RigMainWindow *mw = new RigMainWindow();
    mw->show();

    return app.exec();
}
