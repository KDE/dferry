/*
   Copyright (C) 2026 Andreas Hartmetz <ahartmetz@gmail.com>

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
// _TsnipBegin_TestIncludes

#include <argumentsreader.h>
#include "../tests/testutil.h"

#include <cstring>
#include <iostream>
#include <unordered_map>
// _TsnipEnd_TestIncludes

// _TsnipBegin_TestHelpers
struct TestConsumerBase
{
    TestConsumerBase() : m_parallelReader(Arguments()) {}
    virtual ~TestConsumerBase() = default;

    void testReadAll();
    virtual void cgReaderReadAll() = 0;
    void handleAggregates();

    ArgumentsReader m_parallelReader;
};

void TestConsumerBase::testReadAll()
{
    handleAggregates(); // for any aggregates opening before the first payload item
    cgReaderReadAll();
}

void TestConsumerBase::handleAggregates()
{
    while (true) {
        switch (m_parallelReader.state()) {
        case Arguments::BeginStruct:
            m_parallelReader.beginStruct();
            break;
        case Arguments::EndStruct:
            m_parallelReader.endStruct();
            break;
        case Arguments::BeginVariant:
            TEST(false); // not supported yet
            //m_parallelReader.beginVariant();
            break;
        case Arguments::EndVariant:
            TEST(false); // not supported yet
            m_parallelReader.endVariant();
            break;
        case Arguments::BeginArray:
            m_parallelReader.beginArray();
            break;
        case Arguments::EndArray:
            m_parallelReader.endArray();
            break;
        case Arguments::BeginDict:
            m_parallelReader.beginDict();
            break;
        case Arguments::EndDict:
            m_parallelReader.endDict();
            break;
        default:
            return;
        }
    }
}
// _TsnipEnd_TestHelpers

// _TsnipBegin_CgReadTester

struct _Tvar_TestConsumer : public TestConsumerBase
{
    void cgReaderReadAll() override
    {
        reinterpret_cast<_Tvar_CgReader<_Tvar_TestConsumer>*>(this)->readAll();
    }
    // _TsnipBegin_ProcessArgPrimitive

    void _Tvar_ReceiverName(_Tvar_ArgParameterType arg)
    {
        TEST(m_parallelReader.read_Tvar_ArgReadType() == arg);
        handleAggregates();
    }
    // _TsnipEnd_ProcessArgPrimitive
    // _TsnipBegin_ProcessArgString

    void _Tvar_ReceiverName(const char *ptr, uint32 len)
    {
        const cstring pcs = m_parallelReader.read_Tvar_ArgReadType();
        TEST(len == pcs.length);
        TEST(memcmp(ptr, pcs.ptr, len) == 0);
        handleAggregates();
    }
    // _TsnipEnd_ProcessArgString
    // _Tinsert_ProcessArgDefinitions
};
// _TsnipEnd_CgReadTester

// _TsnipBegin_TestReadFunc

void testCgReader(const Arguments &args)
{
    static const std::unordered_map<std::string,
                                    std::function<std::unique_ptr<TestConsumerBase>(const Arguments&)>>
        testers{
        // _TsnipBegin_TesterMapEntry
        {"_Tvar_TesterSignature", [](const Arguments& args) {
            auto* ret = new _Tvar_CgReader<_Tvar_TestConsumer>(args);
            ret->m_parallelReader = ArgumentsReader(args);
            return std::unique_ptr<TestConsumerBase>(ret);
        }},
        // _TsnipEnd_TesterMapEntry
        // _Tinsert_TesterMapEntries
    };

    const cstring sig = args.signature();
    auto it = testers.find(std::string(sig.ptr, sig.length));
    if (it == testers.cend()) {
        std::cerr << "Warning: testCgReader: no reader found for signature \"" << sig.ptr
                  << "\", will skip testing for it.\n";
        return;
    }
    std::unique_ptr<TestConsumerBase> uTester = it->second(args);
    TestConsumerBase *tester = uTester.get();

    // Note: We don't compare read positions as such (except for that one vital isFinished() check).
    // Currently, Cgeader only processes payload data - all information about position and aggregates needs
    // to be inferred by client code from the sequence of processArg() calls. That is probably OK for regular
    // use and it seems like a waste of effort to extend CgReader with explicit aggregate processing only for
    // testing purposes.
    // A way to get some position information is to give all payload data different values, so different
    // position = different value. That is a best practice for all serialization tests anyway.

    tester->testReadAll();

    TEST(tester->m_parallelReader.isFinished());
    TEST(!tester->m_parallelReader.isError());
}
// _TsnipEnd_TestReadFunc
