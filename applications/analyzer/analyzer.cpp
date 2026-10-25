// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include <QApplication>

#include "analyzermainwindow.h"

int main (int argc, char *argv[])
{
    QApplication app(argc, argv);

    AnalyzerMainWindow *mw = new AnalyzerMainWindow();
    mw->show();

    return app.exec();
}
