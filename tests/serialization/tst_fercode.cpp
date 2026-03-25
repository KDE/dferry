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

static void test_basicEncode()
{
    std::vector<FerCode> ops;

    ops = ferCodeForSignature(cstring(""));
    TEST((ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::NotStarted},
        FerOp{0, FerOpcode::End, Arguments::Finished}}));

    ops = ferCodeForSignature(cstring("i"));
    TEST((ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::NotStarted},
        FerOp{0, FerOpcode::Copy4, Arguments::Int32},
        FerOp{0, FerOpcode::End, Arguments::Finished}}));

    ops = ferCodeForSignature(cstring("x"));
    TEST((ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::NotStarted},
        FerOp{0, FerOpcode::Copy8, Arguments::Int64},
        FerOp{0, FerOpcode::End, Arguments::Finished}}));

    ops = ferCodeForSignature(cstring("ixoo"));
    TEST((ops == std::vector<FerCode>{
        FerOp{0, FerOpcode::BeginMethodSignature, Arguments::NotStarted},
        FerOp{3, FerOpcode::Copy4, Arguments::Int32},
        FerOp{0, FerOpcode::Copy8, Arguments::Int64},
        FerOp{0, FerOpcode::ObjectPath, Arguments::ObjectPath},
        FerOp{0, FerOpcode::ObjectPath, Arguments::ObjectPath},
        FerOp{0, FerOpcode::End, Arguments::Finished}}));

    // TODO check that EnterVariant has the alignment for the element after the variant

}

static void test_variantSignature()
{
    std::vector<FerCode> ops;

    ops = ferCodeForSignature(cstring(""), Arguments::VariantSignature);
    TEST(ops.empty());

    ops = ferCodeForSignature(cstring("i"), Arguments::VariantSignature);
    TEST((ops == std::vector<FerCode>{
        FerBeginVariantSpecial{2, FerOpcode::BeginVariantSignature, 0},
        FerNesting{0, 0},
        FerOp{0, FerOpcode::Copy4, Arguments::Int32},
        FerOp{0, FerOpcode::EndVariant, Arguments::EndVariant}}));

    ops = ferCodeForSignature(cstring("(ixix)"), Arguments::VariantSignature);
    std::cout << printableFerOps(ops) << '\n';
    TEST((ops == std::vector<FerCode>{
        FerBeginVariantSpecial{3, FerOpcode::BeginVariantSignature, 1},
        FerNesting{0, 1},
        FerOp{0, FerOpcode::Copy0, Arguments::BeginDict},
        FerOp{3, FerOpcode::Copy4, Arguments::Int32},
        FerOp{0, FerOpcode::Copy8, Arguments::Int64},
        FerOp{3, FerOpcode::Copy4, Arguments::Int32},
        FerOp{0, FerOpcode::Copy8, Arguments::Int64},
        FerOp{0, FerOpcode::Copy0, Arguments::EndDict},
        FerOp{0, FerOpcode::EndVariant, Arguments::EndVariant}}));
}

static void test_arrayEncode()
{
    std::vector<FerCode> ops;

    ops = ferCodeForSignature(cstring("axoo"));
    std::cout << printableFerOps(ops) << '\n';

    ops = ferCodeForSignature(cstring("asixobiobxobn"));
    std::cout << printableFerOps(ops) << '\n';

    //std::cout << printableFerOps(ops) << '\n';
}

static void test_reader_basic()
{
    Arguments::Writer writer;
    writer.writeUint32(123);
    Arguments arg = writer.finish();

    Arguments::BcReader reader(arg);
    TEST(reader.state() == Arguments::Uint32);
    uint32 res = reader.readUint32();
    TEST(res == 123);
    TEST(reader.state() == Arguments::Finished);
}

int main(int, char *[])
{
    test_basicEncode();
    test_variantSignature();
    test_arrayEncode();
    test_reader_basic();

    std::cout << "Passed!\n";
}
