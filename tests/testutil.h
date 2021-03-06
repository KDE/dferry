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
