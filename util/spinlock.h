/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef SPINLOCK_H
#define SPINLOCK_H

#include <atomic>
#include <cassert>

#ifdef HAVE_VALGRIND
#include <valgrind/helgrind.h>
#else
#include "valgrind-noop.h"
#endif

class Spinlock
{
public:
    Spinlock()
    {
        VALGRIND_HG_MUTEX_INIT_POST(this, 0);
    }

    // The assertion does two things (in debug mode):
    // - Check that a locked Spinlock is not destroyed
    // - Check that a destroyed Spinlock is not locked, by forcing a deadlock in that case
    //   (if the memory has not yet been overwritten)
    ~Spinlock()
    {
        VALGRIND_HG_MUTEX_DESTROY_PRE(this);
        assert(!m_locked.test_and_set(std::memory_order_acquire));
    }

    void lock()
    {
        VALGRIND_HG_MUTEX_LOCK_PRE(this, 0);
        while (m_locked.test_and_set(std::memory_order_acquire)) {
            // spin until locked
        }
        VALGRIND_HG_MUTEX_LOCK_POST(this);
    }

    void unlock()
    {
        VALGRIND_HG_MUTEX_UNLOCK_PRE(this);
        m_locked.clear(std::memory_order_release);
        VALGRIND_HG_MUTEX_UNLOCK_POST(this);
    }
private:
    std::atomic_flag m_locked = ATOMIC_FLAG_INIT;
};

class SpinLocker
{
public:
    SpinLocker(Spinlock *lock)
       : m_lock(lock)
    {
        m_lock->lock();
    }

    ~SpinLocker()
    {
        m_lock->unlock();
    }
private:
    Spinlock *m_lock;
};

#endif // SPINLOCK_H
