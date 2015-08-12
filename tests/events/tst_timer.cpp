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
#include "icompletionclient.h"
#include "platformtime.h"
#include "timer.h"

#include "../testutil.h"

#include <iostream>

class BamPrinter : public ICompletionClient
{
public:
    BamPrinter(const char *customMessage, uint64 startTime)
       : m_customMessage(customMessage), m_startTime(startTime) {}
    void notifyCompletion(void *task) override
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
    t.setCompletionClient(&printer1);
    t.setInterval(231);
    t.setRunning(true);

    const char *customMessage2 = "Hello, world 2!";
    BamPrinter printer2(customMessage2, baseTime);

    Timer t2(&dispatcher);
    t2.setCompletionClient(&printer2);
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
    t3.setCompletionClient(&booPrinter);
    t3.setInterval(420);
    t3.setRunning(true);

    while (dispatcher.poll()) {
    }
}

class AccuracyTester : public ICompletionClient
{
public:
    AccuracyTester()
       : m_lastTriggerTime(PlatformTime::monotonicMsecs())
    {}
    void notifyCompletion(void *task) override
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
    t1.setCompletionClient(&at1);
    t1.setInterval(225);
    t1.setRunning(true);

    AccuracyTester at2;
    Timer t2(&dispatcher);
    t2.setCompletionClient(&at2);
    t2.setInterval(42);
    t2.setRunning(true);

    while (dispatcher.poll()) {
    }
}

// this not only bounds how long the dispatcher runs, it also creates another timer to make the
// situation more interesting
class EventDispatcherInterruptor : public ICompletionClient
{
public:
    EventDispatcherInterruptor(EventDispatcher *ed, int timeout)
       : m_ttl(ed)
    {
        m_ttl.setInterval(timeout);
        m_ttl.setCompletionClient(this);
        m_ttl.setRunning(true);
    }
    void notifyCompletion(void * /*task*/) override
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
    t1->setCompletionClient(&deleter);
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
                t2->setCompletionClient(&iterChecker);
                t2->setRunning(true);
                // this could go wrong because we manipulate the due time in EventDispatcher::addTimer(),
                // but should be caught in Timer::remainingTime()
                TEST(t2->remainingTime() == 0);
            }
        });

        t1.setInterval(10);
        t1.setRunning(true);
        t1.setCompletionClient(&adder);

        EventDispatcherInterruptor interruptor(&dispatcher, 50);

        while (dispatcher.poll()) {
            dispatchCounter++;
        }
        TEST(t2Counter > 1);
        delete t2;
    }
}

static void testTriggerOnlyOncePerDispatch()
{
    EventDispatcher dispatcher;
    int dispatchCounter = 0;
    int noWorkCounter1 = 0;
    int noWorkCounter2 = 0;
    int hardWorkCounter = 0;

    Timer t1(&dispatcher);
    t1.setRunning(true);

    Timer t2(&dispatcher);
    t2.setRunning(true);

    Timer t3(&dispatcher);
    t3.setRunning(true);

    CompletionFunc noWorkCounter([&noWorkCounter1, &noWorkCounter2, &dispatchCounter, &t1, &t3] (void *task)
    {
        if (task == &t1) {
            TEST(noWorkCounter1 == dispatchCounter);
            noWorkCounter1++;
        } else {
            TEST(task == &t3);
            TEST(noWorkCounter2 == dispatchCounter);
            noWorkCounter2++;
        }
    });
    t1.setCompletionClient(&noWorkCounter);
    t3.setCompletionClient(&noWorkCounter);


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
    t2.setCompletionClient(&hardWorker);

    EventDispatcherInterruptor interruptor(&dispatcher, 200);

    while (dispatcher.poll()) {
        dispatchCounter++;
    }

    TEST(noWorkCounter1 == dispatchCounter || noWorkCounter1 == dispatchCounter - 1);
    TEST(noWorkCounter2 == dispatchCounter || noWorkCounter2 == dispatchCounter - 1);
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
    slow.setCompletionClient(&slowReEnabler);
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
    fast.setCompletionClient(&fastReEnabler);
    fast.setRepeating(false);
    fast.setInterval(0);
    fast.setRunning(true);

    // also make sure that setRepeating(false) has any effect at all...
    int noRepeatCounter = 0;
    CompletionFunc noRepeatCheck([&noRepeatCounter] (void * /*task*/) {
        noRepeatCounter++;
    });
    Timer noRepeat(&dispatcher);
    noRepeat.setCompletionClient(&noRepeatCheck);
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

int main(int, char *[])
{
    testBasic();
    testAccuracy();
    testDeleteInTrigger();
    testAddInTrigger();
    testTriggerOnlyOncePerDispatch();
    testReEnableNonRepeatingInTrigger();
    std::cout << "Passed!\n";
}
