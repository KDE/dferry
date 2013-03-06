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
