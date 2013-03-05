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
