// SPDX-FileCopyrightText: 2026 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "arguments.h"
#include "argumentswriter.h"
#include "error.h"

#include <iostream>

static void printInfo(const char* name, const Arguments &args)
{
    if (!args.error().isError()) {
        const char *const sig = args.signature().ptr;
        std::cout << "Type signature for " << name << ": " << (sig ? sig : "") << '\n';
        std::cout << "Contents:\n" << args.prettyPrint();
    } else {
        std::cout << "Error writing Arguments for " << name << ": " << args.error().string() << '\n';
    }
    std::cout << '\n';
}

int main(int, char *[])
{
    // Empty Arguments
    {
        Arguments args;
        printInfo("empty Arguments", args);
    }

    // A single uint32
    {
        ArgumentsWriter writer;
        writer.writeUint32(123);
        Arguments args = writer.finish();
        printInfo("single uint32", args);
    }

    // String
    {
        ArgumentsWriter writer;
        writer.writeString("Hello, world!");
        Arguments args = writer.finish();
        printInfo("string", args);
    }

    // Multiple arguments
    {
        ArgumentsWriter writer;
        writer.writeUint32(42);
        writer.writeString("Moooo!");
        Arguments args = writer.finish();
        printInfo("multiple arguments", args);
    }

    // Struct
    {
        ArgumentsWriter writer;
        writer.beginStruct();
            writer.writeUint32(4096);
            writer.writeString("Moin!");
        writer.endStruct();
        Arguments args = writer.finish();
        printInfo("struct", args);
    }

    // Array
    {
        ArgumentsWriter writer;
        writer.beginArray();
            writer.writeUint32(1001);
        writer.endArray();
        Arguments args = writer.finish();
        printInfo("array with one uint32", args);
    }

    // Dict
    {
        ArgumentsWriter writer;
        writer.beginDict();
            writer.writeString("key1");
            writer.writeString("value1");
        writer.endDict();
        Arguments args = writer.finish();
        printInfo("dict", args);
    }

    // Array of structs (two elements)
    {
        ArgumentsWriter writer;
        writer.beginArray();
            writer.beginStruct();
                writer.writeUint32(1);
                writer.writeString("Hello, you!");
            writer.endStruct();
            writer.beginStruct();
                writer.writeUint32(2);
                writer.writeString("Hello everyone!!");
            writer.endStruct();
        writer.endArray();
        Arguments args = writer.finish();
        printInfo("array of structs (two elements)", args);
    }

    // Empty array of structs (zero elements)
    {
        ArgumentsWriter writer;
        writer.beginArray(ArgumentsWriter::WriteTypesOfEmptyArray);
            writer.beginStruct();
                writer.writeUint32(42); // type used, value ignored
                writer.writeString("Hello, world!"); // type used, value ignored
            writer.endStruct();
        writer.endArray();
        Arguments args = writer.finish();
        printInfo("empty array of structs", args);
    }

    // Variant map - commonly used to encode flexible additional parameters or property maps
    {
        ArgumentsWriter writer;
        writer.beginDict();
            writer.writeString("name");
            writer.beginVariant();
                writer.writeString("John Doe");
            writer.endVariant();

            writer.writeString("age");
            writer.beginVariant();
                writer.writeUint32(42);
            writer.endVariant();

            writer.writeString("ice cream preferences");
            writer.beginVariant();
                writer.beginArray();
                    writer.writeString("pistachio");
                    writer.writeString("malaga");
                    writer.writeString("crème brûlée"); // if that ice cream flavor doesn't exist, it should!
                writer.endArray();
            writer.endVariant();
        writer.endDict();
        Arguments args = writer.finish();
        printInfo("variant map", args);
    }

    // Empty variant map - also demonstrates how to write an empty variant for that
    {
        ArgumentsWriter writer;
        writer.beginDict(ArgumentsWriter::WriteTypesOfEmptyArray);
            writer.writeString("whatever");
            writer.beginVariant();
            // Nothing in between, only possible when writing the types of an empty array (or dict)
            writer.endVariant();
        writer.endDict();
        Arguments args = writer.finish();
        printInfo("empty variant map", args);
    }

    // Error (did not close struct)
    {
        ArgumentsWriter writer;
        writer.beginStruct();
            writer.writeUint32(42);
            writer.writeString("Hello, world!");
        //writer.endStruct(); // Oops, forgot to close the struct!
        Arguments args = writer.finish();
        printInfo("did not close struct", args);
    }

    // Error (mismatched types in array)
    {
        ArgumentsWriter writer;
        writer.beginArray();
            writer.writeUint32(42);
            writer.writeString("I am not a number!");
        writer.endArray();
        Arguments args = writer.finish();
        printInfo("mismatched types in array", args);
    }

    return 0;
}
