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

#include "dselrig_part.h"

#include "mainwidget.h"

#include <kdemacros.h>
#include <kparts/genericfactory.h>

K_PLUGIN_FACTORY(DselRigPartFactory, registerPlugin<DselRigPart>();)  // produce a factory
K_EXPORT_PLUGIN(DselRigPartFactory("DselRig", "DselRig"))

DselRigPart::DselRigPart(QWidget *parentWidget, QObject *parent, const QVariantList &)
   : KParts::ReadOnlyPart(parent)
{
    KGlobal::locale()->insertCatalog("DselRig");
    setComponentData(DselRigPartFactory::componentData());

    QWidget *mainWidget = new MainWidget();
    setWidget(mainWidget);
}

DselRigPart::~DselRigPart()
{
}
