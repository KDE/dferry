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

#include "../../serialization/fercode_p.h"

#include "../testutil.h"

#include <iostream>

#ifdef HAVE_BOOST
template<class T> using my_shared_ptr = boost::local_shared_ptr<T>;
#else
template<class T> using my_shared_ptr = std::shared_ptr<T>;
#endif

static void test_basicEncode()
{
    my_shared_ptr<std::vector<FerCode>> ops;

    ops = ferCodeForSignature(cstring(""));
    TEST((*ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::Finished},
        FerOp{0, FerOpcode::End, Arguments::InvalidData}}));

    ops = ferCodeForSignature(cstring("i"));
    TEST((*ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::Int32},
        FerOp{0, FerOpcode::Copy4, Arguments::Finished},
        FerOp{0, FerOpcode::End, Arguments::InvalidData}}));

    ops = ferCodeForSignature(cstring("x"));
    TEST((*ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::Int64},
        FerOp{0, FerOpcode::Copy8, Arguments::Finished},
        FerOp{0, FerOpcode::End, Arguments::InvalidData}}));

    ops = ferCodeForSignature(cstring("ixoo"));
    TEST((*ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::Int32},
        FerOp{3, FerOpcode::Copy4, Arguments::Int64},
        FerOp{0, FerOpcode::Copy8, Arguments::ObjectPath},
        FerOp{0, FerOpcode::ObjectPath, Arguments::ObjectPath},
        FerOp{0, FerOpcode::ObjectPath, Arguments::Finished},
        FerOp{0, FerOpcode::End, Arguments::InvalidData}}));

    // TODO check that EnterVariant has the alignment for the element after the variant

}

static void test_variantSignature()
{
    my_shared_ptr<std::vector<FerCode>> ops;

    ops = ferCodeForSignature(cstring(""), Arguments::VariantSignature);
    TEST(ops->empty());

    ops = ferCodeForSignature(cstring("i"), Arguments::VariantSignature);
    TEST((*ops == std::vector<FerCode>{
        FerOp{2, FerOpcode::BeginVariantSignature, Arguments::Int32},
        FerNesting{0, 0},
        FerNesting{0, 0},
        FerOp{0, FerOpcode::Copy4, Arguments::EndVariant},
        FerOp{0, FerOpcode::EndVariantSignature, Arguments::InvalidData}}));

    ops = ferCodeForSignature(cstring("(ixix)"), Arguments::VariantSignature);
    TEST((*ops == std::vector<FerCode>{
        FerOp{3, FerOpcode::BeginVariantSignature, Arguments::BeginStruct},
        FerNesting{0, 1},
        FerNesting{1, 0},
        FerOp{0, FerOpcode::Copy0, Arguments::Int32},
        FerOp{3, FerOpcode::Copy4, Arguments::Int64},
        FerOp{0, FerOpcode::Copy8, Arguments::Int32},
        FerOp{3, FerOpcode::Copy4, Arguments::Int64},
        FerOp{0, FerOpcode::Copy8, Arguments::EndStruct},
        FerOp{0, FerOpcode::Copy0, Arguments::EndVariant},
        FerOp{0, FerOpcode::EndVariantSignature, Arguments::InvalidData}}));
}

static void test_arrayEncode()
{
    my_shared_ptr<std::vector<FerCode>> ops;

    ops = ferCodeForSignature(cstring("axoo"));
    //std::cout << printableFerOps(ops) << '\n';

    ops = ferCodeForSignature(cstring("asixobiobxobn"));
    //std::cout << printableFerOps(ops) << '\n';

    //std::cout << "\n\nDICTUM\n\n";
    ops = ferCodeForSignature(cstring("a{is}"));
    //std::cout << printableFerOps(ops) << '\n';

    //std::cout << printableFerOps(ops) << '\n';
}

static void test_reader_basic()
{
    {
        Arguments::Writer writer;
        writer.writeUint32(123);
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::Finished);
    }
    {
        Arguments::Writer writer;
        writer.writeUint32(123);
        writer.writeUint64(123123123123123123);
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::Uint64);
        TEST(reader.readUint64() == 123123123123123123);
        TEST(reader.state() == Arguments::Finished);
    }
    // TODO strings
    {
        Arguments::Writer writer;
        writer.writeUint32(123);
        writer.beginStruct();
        writer.writeUint32(444);
        writer.writeUint64(99999999999999);
        writer.endStruct();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::BeginStruct);
        reader.beginStruct();
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 444);
        TEST(reader.state() == Arguments::Uint64);
        TEST(reader.readUint64() == 99999999999999);
        TEST(reader.state() == Arguments::EndStruct);
        reader.endStruct();
        TEST(reader.state() == Arguments::Finished);
    }
}

static void test_reader_array()
{
    // TODO also:
    // - empty arrays / dicts
    // - structs as array or dict values

    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.writeUint32(123);
        writer.endArray();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginArray);
        reader.beginArray();
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::EndArray);
        reader.endArray();
        TEST(reader.state() == Arguments::Finished);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.writeUint32(123);
        writer.writeUint32(234);
        writer.endArray();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginArray);
        reader.beginArray();
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 234);
        TEST(reader.state() == Arguments::EndArray);
        reader.endArray();
        TEST(reader.state() == Arguments::Finished);
    }
    {
        Arguments::Writer writer;
        writer.beginArray();
        writer.writeUint32(123);
        writer.writeUint32(234);
        writer.endArray();
        writer.writeUint64(12345678901234);
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginArray);
        reader.beginArray();
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 123);
        TEST(reader.state() == Arguments::Uint32);
        TEST(reader.readUint32() == 234);
        TEST(reader.state() == Arguments::EndArray);
        reader.endArray();
        TEST(reader.state() == Arguments::Uint64);
        TEST(reader.readUint64() == 12345678901234);
        TEST(reader.state() == Arguments::Finished);
    }

    {
        Arguments::Writer writer;
        writer.beginDict();
        writer.writeByte(12);
        writer.writeByte(123);
        writer.endDict();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginDict);
        reader.beginDict();
        TEST(reader.state() == Arguments::Byte);
        TEST(reader.readByte() == 12);
        TEST(reader.state() == Arguments::Byte);
        TEST(reader.readByte() == 123);
        TEST(reader.state() == Arguments::EndDict);
        reader.endDict();
        TEST(reader.state() == Arguments::Finished);
    }
}

static void test_reader_variant()
{
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeByte(12);
        writer.endVariant();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginVariant);
        reader.beginVariant();
        TEST(reader.state() == Arguments::Byte);
        TEST(reader.readByte() == 12);
        TEST(reader.state() == Arguments::EndVariant);
        reader.endVariant();
    }
    {
        Arguments::Writer writer;
        writer.beginVariant();
        writer.writeUint64(12345678901234);
        writer.endVariant();
        const Arguments arg = writer.finish();

        Arguments::BcReader reader(arg);
        TEST(reader.state() == Arguments::BeginVariant);
        reader.beginVariant();
        TEST(reader.state() == Arguments::Uint64);
        TEST(reader.readUint64() == 12345678901234);
        TEST(reader.state() == Arguments::EndVariant);
        reader.endVariant();
    }
}

#include <chrono>

static void test_benchmark()
{
    {
        Arguments::Writer writer;
        writer.beginArray();
        static constexpr int repCount = 100000;
        static constexpr int arrayCount = 1;
        for (int i = 0; i < arrayCount; i++) {
            writer.beginStruct();
            writer.writeUint64(12345678901234);
            writer.writeByte(12);
            writer.writeUint32(1234567890);
            writer.writeUint32(34567890);
            writer.endStruct();
        }
        writer.endArray();
        const Arguments arg = writer.finish();

        const std::chrono::time_point beginBcReader = std::chrono::high_resolution_clock::now();
        for (int j = 0; j < repCount; j++) {
            Arguments::BcReader reader(arg);
            reader.beginArray();
            for (int i = 0; i < arrayCount; i++) {
                reader.beginStruct();
                TEST(reader.readUint64() == 12345678901234);
                TEST(reader.readByte() == 12);
                TEST(reader.readUint32() == 1234567890);
                TEST(reader.readUint32() == 34567890);
                reader.endStruct();
            }
            reader.endArray();
        }

        const std::chrono::time_point beginNormalReader = std::chrono::high_resolution_clock::now();
        for (int j = 0; j < repCount; j++) {
            Arguments::Reader reader(arg);
            reader.beginArray();
            for (int i = 0; i < arrayCount; i++) {
                reader.beginStruct();
                TEST(reader.readUint64() == 12345678901234);
                TEST(reader.readByte() == 12);
                TEST(reader.readUint32() == 1234567890);
                TEST(reader.readUint32() == 34567890);
                reader.endStruct();
            }
            reader.endArray();
        }
        const std::chrono::time_point endNormalReader = std::chrono::high_resolution_clock::now();

        std::cout << "bincode: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(beginNormalReader - beginBcReader).count()
                  << '\n';
        std::cout << "normal: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(endNormalReader - beginNormalReader).count()
                  << '\n';

    }
}

int main(int, char *[])
{
    test_basicEncode();
    test_variantSignature();
    test_arrayEncode();
    test_reader_basic();
    test_reader_array();
    test_reader_variant();
    test_benchmark();

    std::cout << "Passed!\n";
}
