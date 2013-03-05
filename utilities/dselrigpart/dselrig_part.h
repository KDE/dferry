#ifndef DSELRIG_PART_H
#define DSELRIG_PART_H

#include <kparts/part.h>
#include <kparts/factory.h>

class DselRigPart : public KParts::ReadOnlyPart
{
    Q_OBJECT
public:
    DselRigPart(QWidget *parentWidget, QObject *parent, const QVariantList &);
    ~DselRigPart();

    bool openFile() { return true; };
};

#endif // DSELRIG_PART_H
