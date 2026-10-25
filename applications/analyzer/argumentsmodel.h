// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef ARGUMENTSMODEL_H
#define ARGUMENTSMODEL_H

class QAbstractItemModel;
class Message;

QAbstractItemModel* createArgumentsModel(Message *message);

#endif // ARGUMENTSMODEL_H
