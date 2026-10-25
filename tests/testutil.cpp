// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "testutil.h"

void test_fail(const char *cond, const char *file, int line)
{
    fprintf(stderr, "FAILED: \"%s\" at %s:%d\n", cond, file, line);
    exit(1);
}
