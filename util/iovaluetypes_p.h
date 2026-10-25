// SPDX-FileCopyrightText: 2018 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef IOVALUETYPES_P_H
#define IOVALUETYPES_P_H

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

#endif // IOVALUETYPES_P_H
