/*
   Copyright (C) 2016 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "arguments.h"

#include "../testutil.h"

#include <iostream>

// ### should probably be officially exposed by the API
enum {
    SpecMaxArrayLength = 67108864, // 64 MiB
    SpecMaxMessageLength = 134217728 // 128 MiB
};

static void test_payloadLengths()
{
    const uint32 maxInt32Count = SpecMaxArrayLength / sizeof(uint32);
    {
        Arguments::Writer writer;
        writer.beginArray();
        for (uint32 i = 0; i < maxInt32Count; i++) {
            writer.writeUint32(i);
        }
        writer.endArray();
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        for (uint32 i = 0; i < maxInt32Count + 1; i++) {
            writer.writeUint32(i);
        }
        writer.endArray();
        // The goal of the next check is to ensure that bevior doesn't change by accident, even
        // though the behavior may be dubious. Because it is dubious, it may still get changed.
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::InvalidData);
    }
    // Note: No need to test dicts, they are implemented pretty much like arrays regarding limits

    // The following two tests are overspecific to the implementation - it can only "guess" the full final
    // message size because it simply isn't known in the ArgumentList. Still better than nothing.
    {
        Arguments::Writer writer;
        for (uint32 i = 0; i < 2; i++) {
            writer.beginArray();
            // -2 because array length prefix adds one uint32 per array, and we must also subtract
            // (signature + alignment padding to 8 byte boundary), i.e. 8 bytes, / 2 = one more
            // sizeof(uint32) per array.
            for (uint32 j = 0; j < maxInt32Count - 2; j++) {
                writer.writeUint32(j);
            }
            writer.endArray();
        }
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::Finished);
    }
    {
        Arguments::Writer writer;
        for (uint32 i = 0; i < 2; i++) {
            writer.beginArray();
            for (uint32 j = 0; j < maxInt32Count - 1; j++) {
                writer.writeUint32(j);
            }
            writer.endArray();
        }
        writer.writeByte(123); // one byte too many!
        TEST(writer.state() != Arguments::InvalidData);
        Arguments arg = writer.finish();
        TEST(writer.state() == Arguments::InvalidData);
    }
}

int main(int, char *[])
{
    test_payloadLengths();
    std::cout << "Passed!\n";
}
