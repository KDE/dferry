// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef TESTUTIL_H
#define TESTUTIL_H

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32

#ifdef BUILDING_TESTUTIL
#define TESTUTIL_EXPORT __declspec(dllexport)
#else
#define TESTUTIL_EXPORT __declspec(dllimport)
#endif

#else

#ifdef BUILDING_TESTUTIL
#define TESTUTIL_EXPORT __attribute__ ((visibility ("protected")))
#else
#define TESTUTIL_EXPORT __attribute__ ((visibility ("default")))
#endif

#endif // _WIN32

static inline void test_no_op() {}
void TESTUTIL_EXPORT test_fail(const char *cond, const char *file, int line);

#define TEST(cond) (!(cond) ? test_fail(#cond, __FILE__, __LINE__) : test_no_op())

#endif // TESTUTIL_H
