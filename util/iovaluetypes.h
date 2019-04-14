/*
   Copyright (C) 2018 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef IOVALUETYPES_H
#define IOVALUETYPES_H

#include "types.h"

namespace IO
{

// would be nice to wrap this into a type-safe bitset enum / class, but since it's for
// internal use, uint32 is okay...
enum class RW
{
    Read = 1,
    Write = 2,
};

enum class Status
{
    OK = 0,
    RemoteClosed,
    LocalClosed,
    PayloadError,
    InternalError
};

struct Result
{
    Status status = Status::OK;
    uint32 length = 0;
};

} // namespace IO

#endif // IOVALUETYPES_H
