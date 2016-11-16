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
#include "arguments.h"

#include <QStandardItemModel>

static void addKeyValue(QStandardItem *parent, const char *k, bool isEmpty, const QVariant &v)
{
    QStandardItem *key = new QStandardItem(QLatin1String(k));
    QStandardItem *value = new QStandardItem;
    value->setData(isEmpty ? QLatin1String("<nil>") : v, Qt::DisplayRole);
    parent->appendRow(QList<QStandardItem *>() << key << value);
}

// when isEmpty, str is an invalid pointer. getting the pointer is safe, only dereferencing is not.
static void addKeyValue(QStandardItem *parent, const char *k, bool isEmpty, const char *str)
{
    addKeyValue(parent, k, isEmpty, isEmpty ? QVariant() : QString::fromUtf8(str));
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

    Arguments::Reader reader(message->arguments());
    if (!reader.isValid()) {
        return withFaultyData(model);
    }

    bool isDone = false;
    // Cache it, don't call Reader::isInsideEmptyArray() on every data element.
    bool inEmptyArray = false;

    while (!isDone) {
        switch(reader.state()) {
        case Arguments::Finished:
            isDone = true;
            break;
        case Arguments::BeginStruct:
            reader.beginStruct();
            parent = descend(parent, "Struct");
            break;
        case Arguments::EndStruct:
            reader.endStruct();
            parent = ascend(parent, model);
            break;
        case Arguments::BeginVariant:
            reader.beginVariant();
            parent = descend(parent, "Variant");
            break;
        case Arguments::EndVariant:
            reader.endVariant();
            parent = ascend(parent, model);
            break;
        case Arguments::BeginArray: {
            inEmptyArray = !reader.beginArray(Arguments::Reader::ReadTypesOnlyIfEmpty);
            parent = descend(parent, inEmptyArray ? "Array (no elements, showing just types)" : "Array");
            break; }
        case Arguments::EndArray:
            reader.endArray();
            inEmptyArray = reader.isInsideEmptyArray();
            parent = ascend(parent, model);
            break;
        case Arguments::BeginDict: {
            inEmptyArray = !reader.beginDict(Arguments::Reader::ReadTypesOnlyIfEmpty);
            parent = descend(parent, inEmptyArray ? "Dict (no elements, showing just types)" : "Dict");
            break; }
        case Arguments::EndDict:
            reader.endDict();
            inEmptyArray = reader.isInsideEmptyArray();
            parent = ascend(parent, model);
            break;
        case Arguments::Byte:
            addKeyValue(parent, "byte", inEmptyArray, reader.readByte());
            break;
        case Arguments::Boolean:
            addKeyValue(parent, "boolean", inEmptyArray, reader.readBoolean());
            break;
        case Arguments::Int16:
            addKeyValue(parent, "int16", inEmptyArray, reader.readInt16());
            break;
        case Arguments::Uint16:
            addKeyValue(parent, "uint16", inEmptyArray, reader.readUint16());
            break;
        case Arguments::Int32:
            addKeyValue(parent, "int32", inEmptyArray, reader.readInt32());
            break;
        case Arguments::Uint32:
            addKeyValue(parent, "uint32", inEmptyArray, reader.readUint32());
            break;
        case Arguments::Int64:
            addKeyValue(parent, "int64", inEmptyArray, reader.readInt64());
            break;
        case Arguments::Uint64:
            addKeyValue(parent, "uint64", inEmptyArray, reader.readUint64());
            break;
        case Arguments::Double:
            addKeyValue(parent, "double", inEmptyArray, reader.readDouble());
            break;
        case Arguments::String:
            addKeyValue(parent, "string", inEmptyArray, reader.readString().ptr);
            break;
        case Arguments::ObjectPath:
            addKeyValue(parent, "object path", inEmptyArray, reader.readObjectPath().ptr);
            break;
        case Arguments::Signature:
            addKeyValue(parent, "type signature", inEmptyArray, reader.readSignature().ptr);
            break;
        case Arguments::UnixFd:
            addKeyValue(parent, "file descriptor", inEmptyArray, reader.readUnixFd());
            break;
        case Arguments::InvalidData:
        case Arguments::NeedMoreData:
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
