#include "dselrig_part.h"
#include "eavesdroppermodel.h"

#include <kdemacros.h>
#include <kparts/genericfactory.h>

#include <QLabel>
#include <QTreeView>
#include <QVBoxLayout>

K_PLUGIN_FACTORY(DselRigPartFactory, registerPlugin<DselRigPart>();)  // produce a factory
K_EXPORT_PLUGIN(DselRigPartFactory("DselRig", "DselRig"))

class DselRigPart::Private
{
public:
    QWidget *mainWidget;
};

DselRigPart::DselRigPart(QWidget *parentWidget, QObject *parent, const QVariantList &)
   : KParts::ReadOnlyPart(parent),
     d(new Private)
{
    KGlobal::locale()->insertCatalog("DselRig");
    setComponentData(DselRigPartFactory::componentData());

    d->mainWidget = new QWidget();
    QVBoxLayout *mainLayout = new QVBoxLayout;
    d->mainWidget->setLayout(mainLayout);

    QTreeView *messageList = new QTreeView;
    mainLayout->addWidget(messageList);
    EavesdropperModel *model = new EavesdropperModel();
    messageList->setModel(model);

    setWidget(d->mainWidget);
}

DselRigPart::~DselRigPart()
{
    delete d;
    d = 0;
}
