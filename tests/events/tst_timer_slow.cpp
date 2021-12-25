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

#include "eventdispatcher.h"
#include "icompletionlistener.h"
#include "platformtime.h"
#include "timer.h"

#include "../testutil.h"

#include <cstring>
#include <iostream>

class BamPrinter : public ICompletionListener
{
public:
    BamPrinter(const char *customMessage, uint64 startTime)
       : m_customMessage(customMessage), m_startTime(startTime) {}
    void handleCompletion(void *task) override
    {
        uint64 timeDiff = PlatformTime::monotonicMsecs() - m_startTime;
        std::cout << "BAM " << task << ' ' << timeDiff << ' ' << m_customMessage << " #" << m_counter++ << '\n';
    }
    const char *m_customMessage;
    uint64 m_startTime;
    int m_counter = 0;
};

// supposed to print some output to prove timers are working, and not crash :)
static void testBasic()
{
    EventDispatcher dispatcher;
    uint64 baseTime = PlatformTime::monotonicMsecs();

    const char *customMessage1 = "Hello, world 1!";
    BamPrinter printer1(customMessage1, baseTime);

    Timer t(&dispatcher);
    t.setCompletionListener(&printer1);
    t.setInterval(231);
    t.setRunning(true);

    const char *customMessage2 = "Hello, world 2!";
    BamPrinter printer2(customMessage2, baseTime);

    Timer t2(&dispatcher);
    t2.setCompletionListener(&printer2);
    t2.setInterval(100);
    t2.setRunning(true);


    const char *customMessage3 = "Hello, other world!";
    int booCounter = 0;
    CompletionFunc booPrinter([baseTime, customMessage3, &booCounter, &dispatcher, &t] (void *task)
    {
        uint64 timeDiff = PlatformTime::monotonicMsecs() - baseTime;
        std::cout << "boo " << task << ' ' << timeDiff << ' ' << customMessage3 << " #" << booCounter
                  << " - Timer 1 remaining time: " << t.remainingTime() << '\n';
        if (booCounter >= 4) {
            dispatcher.interrupt();
        }
        booCounter++;
    });

    Timer t3(&dispatcher);
    t3.setCompletionListener(&booPrinter);
    t3.setInterval(420);
    t3.setRunning(true);

    while (dispatcher.poll()) {
    }
}

class AccuracyTester : public ICompletionListener
{
public:
    AccuracyTester()
       : m_lastTriggerTime(PlatformTime::monotonicMsecs())
    {}
    void handleCompletion(void *task) override
    {
        Timer *timer = reinterpret_cast<Timer *>(task);
        uint64 currentTime = PlatformTime::monotonicMsecs();
        int timeDiff = int64(currentTime) - int64(m_lastTriggerTime);
        m_lastTriggerTime = currentTime;

        std::cout << timer->interval() << ' ' << timeDiff << std::endl;
        TEST(std::abs(timeDiff - timer->interval()) < 5);

        m_count++;
        TEST(m_count < 26); // event loop should have stopped right at 25

        if (m_count == 25) {
            timer->eventDispatcher()->interrupt();
        }
    }
    uint64 m_lastTriggerTime;
    uint m_count = 0;
};

static void testAccuracy()
{
    // this test is likely to fail spuriously on a machine under load
    EventDispatcher dispatcher;

    AccuracyTester at1;
    Timer t1(&dispatcher);
    t1.setCompletionListener(&at1);
    t1.setInterval(225);
    t1.setRunning(true);

    AccuracyTester at2;
    Timer t2(&dispatcher);
    t2.setCompletionListener(&at2);
    t2.setInterval(42);
    t2.setRunning(true);

    while (dispatcher.poll()) {
    }
}

// this not only bounds how long the dispatcher runs, it also creates another timer to make the
// situation more interesting
class EventDispatcherInterruptor : public ICompletionListener
{
public:
    EventDispatcherInterruptor(EventDispatcher *ed, int timeout)
       : m_ttl(ed)
    {
        m_ttl.setInterval(timeout);
        m_ttl.setCompletionListener(this);
        m_ttl.setRunning(true);
    }
    void handleCompletion(void * /*task*/) override
    {
        m_ttl.eventDispatcher()->interrupt();
        m_ttl.setRunning(false);
    }
    Timer m_ttl;
};

static void testDeleteInTrigger()
{
    EventDispatcher dispatcher;

    bool alreadyCalled = false;
    CompletionFunc deleter([&alreadyCalled] (void *task)
    {
        TEST(!alreadyCalled);
        alreadyCalled = true;
        Timer *timer = reinterpret_cast<Timer *>(task);
        delete timer;
    });

    Timer *t1 = new Timer(&dispatcher);
    t1->setCompletionListener(&deleter);
    t1->setRunning(true);

    EventDispatcherInterruptor interruptor(&dispatcher, 50);

    while (dispatcher.poll()) {
    }
}

static void testAddInTrigger()
{
    // A timer added from the callback of another timer should not trigger in the same event loop
    // iteration, otherwise there could be an (accidental or intended) infinite cascade of zero interval
    // timers adding zero interval timers

    // since this test has a (small) false negative (note: negative == no problem found) rate - if
    // the current millisecond changes at certain points, it can mask a problem - just run it a couple
    // of times...
    for (int i = 0; i < 5; i++) {
        EventDispatcher dispatcher;
        int dispatchCounter = 0;
        int t2Counter = 0;

        CompletionFunc iterChecker([&dispatchCounter, &t2Counter] (void * /*task*/)
        {
            TEST(dispatchCounter > 0);
            t2Counter++;
        });

        Timer t1(&dispatcher);
        Timer *t2 = nullptr;
        CompletionFunc adder([&dispatcher, &t2, &iterChecker] (void * /*task*/)
        {
            if (!t2) {
                t2 = new Timer(&dispatcher);
                t2->setCompletionListener(&iterChecker);
                t2->setRunning(true);
                // this could go wrong because we manipulate the due time in EventDispatcher::addTimer(),
                // but should be caught in Timer::remainingTime()
                TEST(t2->remainingTime() == 0);
            }
        });

        t1.setInterval(10);
        t1.setRunning(true);
        t1.setCompletionListener(&adder);

        EventDispatcherInterruptor interruptor(&dispatcher, 50);

        while (dispatcher.poll()) {
            dispatchCounter++;
        }
        TEST(t2Counter > 1);
        delete t2;
    }
}

static void testReAddInTrigger()
{
    // - Add a timer
    //   - Remove it
    //   - Remove it, then add it
    //   - Remove, add, remove
    //   - Remove, add, remove, add
    // - Check timer's isRunning() considering whether last action was add or remove
    // - Check if the timer triggers next time or not, consistent with previous point


    // Repeat the tests that include re-adding with "pointer aliased" timers, i.e. add a new timer created
    // at the same memory location as the old one. That tests whether a known difficulty of the chosen
    // implementation is handled correctly.

    // Use the array to ensure we have pointer aliasing or no pointer aliasing
    std::aligned_storage<sizeof(Timer)>::type timerStorage[2];
    memset(timerStorage, 0, sizeof(timerStorage));

    Timer *const timerArray = reinterpret_cast<Timer *>(timerStorage);

    for (int i = 0; i < 2; i++) {
        const bool withAliasing = i == 1;

        for (int j = 0; j < 5; j++) { // j = number of add / remove ops
            EventDispatcher dispatcher;

            Timer *t = &timerArray[0];
            bool removeTimer = false;
            bool checkTrigger = false;
            bool didTrigger = false;

            CompletionFunc addRemove([&] (void * /*task*/) {
                if (checkTrigger) {
                    didTrigger = true;
                    return;
                }

                for (int k = 0; k < j; k++) {
                    removeTimer = (k & 1) == 0;
                    if (removeTimer) {
                        TEST(t->isRunning());
                        t->~Timer();
                        // ensure that it can't trigger - of course if Timer
                        // relies on that we should find it in valgrind...
                        memset(static_cast<void *>(t), 0, sizeof(Timer));
                    } else {
                        if (!withAliasing) {
                            if (t == &timerArray[0]) {
                                t = &timerArray[1];
                            } else {
                                t = &timerArray[0];
                            }
                        }
                        new(t) Timer(&dispatcher);
                        t->setCompletionListener(&addRemove);
                        t->start(0);
                        TEST(t->isRunning());
                    }
                }
            });


            Timer dummy1(&dispatcher);
            dummy1.start(0);

            new(t) Timer(&dispatcher);
            t->start(0);

            Timer dummy2(&dispatcher);
            dummy2.start(0);

            dispatcher.poll(); // this seems like a good idea for the test...

            // run and test the add / remove sequence
            t->setCompletionListener(&addRemove);
            dispatcher.poll();

            // Test that the timer triggers when it should. Triggering when it should not will likely
            // cause a segfault or other error because the Timer's memory has been cleared.

            checkTrigger = true;
            dispatcher.poll();
            TEST(didTrigger != removeTimer);

            // clean up
            if (!removeTimer) {
                t->~Timer();
            }
            memset(timerStorage, 0, sizeof(timerStorage));
        }
    }
}

// Test that all 0 msec timers trigger equally often regardless how long their triggered handler takes
static void testTriggerOnlyOncePerDispatch()
{
    EventDispatcher dispatcher;
    int dispatchCounter = 0;
    int triggerCounter1 = 0;
    int triggerCounter2 = 0;
    int hardWorkCounter = 0;

    Timer counter1Timer(&dispatcher);
    counter1Timer.setRunning(true);

    Timer hardWorkTimer(&dispatcher);
    hardWorkTimer.setRunning(true);

    Timer counter2Timer(&dispatcher);
    counter2Timer.setRunning(true);

    CompletionFunc countTriggers([&triggerCounter1, &triggerCounter2, &dispatchCounter,
                                  &counter1Timer, &counter2Timer] (void *task) {
        if (task == &counter1Timer) {
            TEST(triggerCounter1 == dispatchCounter);
            triggerCounter1++;
        } else {
            TEST(task == &counter2Timer);
            TEST(triggerCounter2 == dispatchCounter);
            triggerCounter2++;
        }
    });
    counter1Timer.setCompletionListener(&countTriggers);
    counter2Timer.setCompletionListener(&countTriggers);

    CompletionFunc hardWorker([&hardWorkCounter, &dispatchCounter] (void * /*task*/)
    {
        TEST(hardWorkCounter == dispatchCounter);
        uint64 startTime = PlatformTime::monotonicMsecs();
        // waste ten milliseconds, trying not to spend all time in PlatformTime::monotonicMsecs()
        do {
            for (volatile int i = 0; i < 20000; i++) {}
        } while (PlatformTime::monotonicMsecs() < startTime + 10);
        hardWorkCounter++;
    });
    hardWorkTimer.setCompletionListener(&hardWorker);

    EventDispatcherInterruptor interruptor(&dispatcher, 200);

    while (dispatcher.poll()) {
        dispatchCounter++;
    }

    TEST(triggerCounter1 == dispatchCounter || triggerCounter1 == dispatchCounter - 1);
    TEST(triggerCounter2 == dispatchCounter || triggerCounter2 == dispatchCounter - 1);
    TEST(hardWorkCounter == dispatchCounter || hardWorkCounter == dispatchCounter - 1);
}

static void testReEnableNonRepeatingInTrigger()
{
    EventDispatcher dispatcher;

    int slowCounter = 0;
    CompletionFunc slowReEnabler([&slowCounter] (void *task)
    {
        slowCounter++;
        Timer *timer = reinterpret_cast<Timer *>(task);
        TEST(!timer->isRunning());
        timer->setRunning(true);
        TEST(timer->isRunning());
        TEST(timer->interval() == 5);
    });

    Timer slow(&dispatcher);
    slow.setCompletionListener(&slowReEnabler);
    slow.setRepeating(false);
    slow.setInterval(5);
    slow.setRunning(true);

    int fastCounter = 0;
    CompletionFunc fastReEnabler([&fastCounter] (void *task) {
        fastCounter++;
        Timer *timer = reinterpret_cast<Timer *>(task);
        TEST(!timer->isRunning());
        timer->setRunning(true);
        TEST(timer->isRunning());
        TEST(timer->interval() == 0);
    });

    Timer fast(&dispatcher);
    fast.setCompletionListener(&fastReEnabler);
    fast.setRepeating(false);
    fast.setInterval(0);
    fast.setRunning(true);

    // also make sure that setRepeating(false) has any effect at all...
    int noRepeatCounter = 0;
    CompletionFunc noRepeatCheck([&noRepeatCounter] (void * /*task*/) {
        noRepeatCounter++;
    });
    Timer noRepeat(&dispatcher);
    noRepeat.setCompletionListener(&noRepeatCheck);
    noRepeat.setRepeating(false);
    noRepeat.setInterval(10);
    noRepeat.setRunning(true);

    EventDispatcherInterruptor interruptor(&dispatcher, 50);

    while (dispatcher.poll()) {
    }

    TEST(noRepeatCounter == 1);
    TEST(slowCounter >= 8 && slowCounter <= 12);
    // std::cout << '\n' << fastCounter << ' ' << slowCounter <<'\n';
    TEST(fastCounter >= 200); // ### hopefully low enough even for really slow machines and / or valgrind
}

static void testSerialWraparound()
{
    EventDispatcher dispatcher;

    constexpr int timersCount = 17;
    Timer* timers[timersCount];
    int lastTriggeredTimer;

    CompletionFunc orderCheck([&timers, &lastTriggeredTimer] (void *task) {
        int timerIndex = 0;
        for (; timerIndex < timersCount; timerIndex++) {
            if (timers[timerIndex] == task) {
                break;
            }
        }
        TEST(timerIndex < timersCount);
        TEST(++lastTriggeredTimer == timerIndex);
    });

    // Glassbox testing: we know that the maximum timer serials is 1023, so testing 10k * 17 timers
    // is plenty. This should be adapted if / when the implementation changes.
    for (int i = 0; i < 10000; i++) {
        for (int j = 0; j < 17; j++) {
            timers[j] = new Timer(&dispatcher);
            timers[j]->setCompletionListener(&orderCheck);
            timers[j]->setRunning(true);
        }

        lastTriggeredTimer = -1;

        dispatcher.poll();

        TEST(lastTriggeredTimer == timersCount - 1);

        for (int j = 0; j < 17; j++) {
            delete timers[j];
            timers[j] = nullptr;
        }
    }
}

int main(int, char *[])
{
    testBasic();
    testAccuracy();
    testDeleteInTrigger();
    testAddInTrigger();
    testReAddInTrigger();
    testTriggerOnlyOncePerDispatch();
    testReEnableNonRepeatingInTrigger();
    testSerialWraparound();
    std::cout << "Passed!\n";
}
