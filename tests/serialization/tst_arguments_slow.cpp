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

#include <cstring>
#include <iostream>

static void test_arrayLength()
{
    const uint32 maxInt32Count = Arguments::MaxArrayLength / sizeof(uint32);
    for (int i = 0; i < 2; i++) {
        const bool withVariant = i == 1;
        {
            Arguments::Writer writer;
            if (withVariant) {
                writer.beginVariant();
            }
            writer.beginArray();
            for (uint32 j = 0; j < maxInt32Count; j++) {
                writer.writeUint32(j);
            }
            writer.endArray();
            if (withVariant) {
                TEST(writer.state() != Arguments::InvalidData);
                writer.endVariant();
            }
            Arguments arg = writer.finish();
            TEST(writer.state() == Arguments::Finished);
        }
        {
            Arguments::Writer writer;
            if (withVariant) {
                writer.beginVariant();
            }
            writer.beginArray();
            for (uint32 j = 0; j < maxInt32Count + 1; j++) {
                writer.writeUint32(j);
            }
            writer.endArray();
            if (withVariant) {
                TEST(writer.state() != Arguments::InvalidData);
                writer.endVariant();
            }
            TEST(writer.state() == Arguments::InvalidData);
        }
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

static void test_argumentsLength()
{
    static const uint32 bufferSize = Arguments::MaxArrayLength + 1024;
    byte *buffer = static_cast<byte *>(malloc(bufferSize));
    memset(buffer, 0, bufferSize);

    // Gross max length violations should be caught early
    for (int i = 0; i < 2; i++) {
        const bool withVariant = i == 1;

        Arguments::Writer writer;
        for (int j = 0; j < 4; j++) {
            if (withVariant) {
                writer.beginVariant();
                writer.beginStruct();
            }
            writer.writePrimitiveArray(Arguments::Byte, chunk(buffer, Arguments::MaxArrayLength));
            if (j == 1) {
                // Now just over max size. Require the length check in Writer to be lenient before
                // finish() - that is expected of the current implementation.
                if (withVariant) {
                    for (int k = 0; k <= j; k++) {
                        writer.endStruct();
                        writer.endVariant();
                    }
                }
                Arguments::Writer writer2(writer);
                TEST(writer2.state() != Arguments::InvalidData);
                writer2.finish();
                TEST(writer2.state() == Arguments::InvalidData);
            }
        }
        TEST(writer.state() == Arguments::InvalidData);
    }

    // Test message size exactly at maximum and exactly 1 byte over
    for (int i = 0; i < 4; i++) {
        const bool makeTooLong = i & 1;
        const bool withVariant = i & 2;

        Arguments::Writer writer;
        // note: Arguments does not count Arguments::signature() length towards length
        uint32 left = Arguments::MaxMessageLength;
        writer.writePrimitiveArray(Arguments::Byte, chunk(buffer, Arguments::MaxArrayLength));
        left -= 2 * sizeof(uint32) + Arguments::MaxArrayLength; // sizeof(uint32): array length field
        if (withVariant) {
            left -= 3; // variant signature "s", no alignment before next element
        }
        writer.writePrimitiveArray(Arguments::Byte, chunk(buffer, left - 4));
        // Now there are exactly 4 bytes left to MaxMessageLength

        TEST(writer.state() != Arguments::InvalidData);

        if (withVariant) {
            writer.beginVariant();
        }
        // Write a signature because it requires no alignment and its size can be anything from 2 to 257
        if (makeTooLong) {
            writer.writeSignature(cstring("xxx")); // + one byte length prefix + null terminator = 5 bytes
        } else {
            writer.writeSignature(cstring("xx"));
        }
        if (withVariant) {
            writer.endVariant();
        }
        TEST(writer.state() != Arguments::InvalidData);
        writer.finish();
        if (makeTooLong) {
            TEST(writer.state() == Arguments::InvalidData);
        } else {
            TEST(writer.state() == Arguments::Finished);
        }
    }
}

int main(int, char *[])
{
    test_arrayLength();
    test_argumentsLength();
    std::cout << "Passed!\n";
}
