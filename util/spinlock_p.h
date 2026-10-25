// SPDX-FileCopyrightText: 2014 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef SPINLOCK_P_H
#define SPINLOCK_P_H

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

#endif // SPINLOCK_P_H
