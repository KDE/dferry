// SPDX-FileCopyrightText: 2015 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef VALGRIND_NOOP_H
#define VALGRIND_NOOP_H

#define VALGRIND_HG_MUTEX_INIT_POST(a, b)
#define VALGRIND_HG_MUTEX_DESTROY_PRE(a)
#define VALGRIND_HG_MUTEX_LOCK_PRE(a, b)
#define VALGRIND_HG_MUTEX_LOCK_POST(a)
#define VALGRIND_HG_MUTEX_UNLOCK_PRE(a)
#define VALGRIND_HG_MUTEX_UNLOCK_POST(a)

#endif
