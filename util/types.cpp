// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "types.h"

#include <cstring>

// for now, this .cpp file mainly exists to avoid including cstring in types.h that is supposed
// to include just basic typey stuff.

cstring::cstring(const char *b)
   : ptr(const_cast<char *>(b)),
     length(strlen(b))
{}
