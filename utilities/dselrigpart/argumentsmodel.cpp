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

#include "argumentsmodel.h"

#include "message.h"
#include "argumentlist.h"

#include <QStandardItemModel>

static void addKeyValue(QStandardItem *parent, const char *k, bool isEmpty, const QVariant &v)
{
    QStandardItem *key = new QStandardItem(QLatin1String(k));
    QStandardItem *value = new QStandardItem;
    value->setData(isEmpty ? QLatin1String("<nil>") : v, Qt::DisplayRole);
    parent->appendRow(QList<QStandardItem *>() << key << value);
}

static QStandardItem *ascend(QStandardItem *parent, QStandardItemModel *model)
{
    // the parent of a top-level item is null, not model->invisibleRootItem()...
    QStandardItem *newParent = parent->parent();
    return newParent ? newParent : model->invisibleRootItem();
}

static QStandardItem *descend(QStandardItem *parent, const QString &name)
{
    QStandardItem *newParent = new QStandardItem(name);
    parent->appendRow(newParent);
    return newParent;
}

static QStandardItemModel *withFaultyData(QStandardItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->appendRow(new QStandardItem(QLatin1String("bad data!")));
    return model;
}

static QStandardItemModel *withEmptyData(QStandardItemModel *model)
{
    model->removeRows(0, model->rowCount());
    model->appendRow(new QStandardItem(QLatin1String("<no arguments>")));
    return model;
}

QAbstractItemModel* createArgumentsModel(Message *message)
{
    QStandardItemModel *model = new QStandardItemModel();
    QStringList headerLabels = QStringList() << QLatin1String("Type") << QLatin1String("Value");
    model->setHorizontalHeaderLabels(headerLabels);

    if (!message) {
        return withEmptyData(model);
    }

    QStandardItem *parent = model->invisibleRootItem();

    ArgumentList::ReadCursor reader =
        const_cast<ArgumentList*>(&message->argumentList())->beginRead();
    if (!reader.isValid()) {
        return withFaultyData(model);
    }

    bool isDone = false;
    int emptyNesting = 0;

    while (!isDone) {
        switch(reader.state()) {
        case ArgumentList::Finished:
            isDone = true;
            break;
        case ArgumentList::BeginStruct:
            reader.beginStruct();
            parent = descend(parent, "Struct");
            break;
        case ArgumentList::EndStruct:
            reader.endStruct();
            parent = ascend(parent, model);
            break;
        case ArgumentList::BeginVariant:
            reader.beginVariant();
            parent = descend(parent, "Variant");
            break;
        case ArgumentList::EndVariant:
            reader.endVariant();
            parent = ascend(parent, model);
            break;
        case ArgumentList::BeginArray: {
            bool isEmpty;
            reader.beginArray(&isEmpty);
            parent = descend(parent, isEmpty ? "Array (no elements)" : "Array");
            emptyNesting += isEmpty ? 1 : 0;
            break; }
        case ArgumentList::NextArrayEntry:
            reader.nextArrayEntry();
            break;
        case ArgumentList::EndArray:
            reader.endArray();
            parent = ascend(parent, model);
            emptyNesting = qMax(emptyNesting - 1, 0);
            break;
        case ArgumentList::BeginDict: {
            bool isEmpty = false;
            reader.beginDict(&isEmpty);
            parent = descend(parent, isEmpty ? "Dict (no elements)" : "Dict");
            emptyNesting += isEmpty ? 1 : 0;
            break; }
        case ArgumentList::NextDictEntry:
            reader.nextDictEntry();
            break;
        case ArgumentList::EndDict:
            reader.endDict();
            parent = ascend(parent, model);
            emptyNesting = qMax(emptyNesting - 1, 0);
            break;
        case ArgumentList::Byte:
            addKeyValue(parent, "byte", emptyNesting, reader.readByte());
            break;
        case ArgumentList::Boolean:
            addKeyValue(parent, "boolean", emptyNesting, reader.readBoolean());
            break;
        case ArgumentList::Int16:
            addKeyValue(parent, "int16", emptyNesting, reader.readInt16());
            break;
        case ArgumentList::Uint16:
            addKeyValue(parent, "uint16", emptyNesting, reader.readUint16());
            break;
        case ArgumentList::Int32:
            addKeyValue(parent, "int32", emptyNesting, reader.readInt32());
            break;
        case ArgumentList::Uint32:
            addKeyValue(parent, "uint32", emptyNesting, reader.readUint32());
            break;
        case ArgumentList::Int64:
            addKeyValue(parent, "int64", emptyNesting, reader.readInt64());
            break;
        case ArgumentList::Uint64:
            addKeyValue(parent, "uint64", emptyNesting, reader.readUint64());
            break;
        case ArgumentList::Double:
            addKeyValue(parent, "double", emptyNesting, reader.readDouble());
            break;
        case ArgumentList::String:
            addKeyValue(parent, "string", emptyNesting,
                        QString::fromUtf8(reinterpret_cast<const char *>(reader.readString().begin)));
            break;
        case ArgumentList::ObjectPath:
            addKeyValue(parent, "object path", emptyNesting,
                        QString::fromUtf8(reinterpret_cast<const char *>(reader.readObjectPath().begin)));
            break;
        case ArgumentList::Signature:
            addKeyValue(parent, "type signature", emptyNesting,
                        QString::fromUtf8(reinterpret_cast<const char *>(reader.readSignature().begin)));
            break;
        case ArgumentList::UnixFd:
            addKeyValue(parent, "file descriptor", emptyNesting, QVariant());
            break;
        case ArgumentList::InvalidData:
        case ArgumentList::NeedMoreData:
        default:
            return withFaultyData(model);
            break;
        }
    }

    if (!model->rowCount()) {
        return withEmptyData(model);
    }
    return model;
}
