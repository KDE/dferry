#include "argumentlist.h"

#include "../testutil.h"

#include <cstring>
#include <iostream>

using namespace std;

static void test_stringValidation()
{
    {
        cstring emptyWithNull("");
        cstring emptyWithoutNull;

        TEST(!ArgumentList::isStringValid(emptyWithoutNull));
        TEST(ArgumentList::isStringValid(emptyWithNull));

        TEST(!ArgumentList::isObjectPathValid(emptyWithoutNull));
        TEST(!ArgumentList::isObjectPathValid(emptyWithNull));

        TEST(ArgumentList::isSignatureValid(emptyWithNull));
        TEST(!ArgumentList::isSignatureValid(emptyWithoutNull));
        TEST(ArgumentList::isSignatureValid(emptyWithNull, ArgumentList::VariantSignature));
        TEST(!ArgumentList::isSignatureValid(emptyWithoutNull, ArgumentList::VariantSignature));
    }
    {
        cstring trivial("i");
        TEST(ArgumentList::isSignatureValid(trivial));
        TEST(ArgumentList::isSignatureValid(trivial, ArgumentList::VariantSignature));
    }
    {
        cstring list("iqb");
        TEST(ArgumentList::isSignatureValid(list));
        TEST(!ArgumentList::isSignatureValid(list, ArgumentList::VariantSignature));
        cstring list2("aii");
        TEST(ArgumentList::isSignatureValid(list2));
        TEST(!ArgumentList::isSignatureValid(list2, ArgumentList::VariantSignature));
    }
    {
        cstring simpleArray("ai");
        TEST(ArgumentList::isSignatureValid(simpleArray));
        TEST(ArgumentList::isSignatureValid(simpleArray, ArgumentList::VariantSignature));
    }
    {
        cstring messyArray("a(iaia{ia{iv}})");
        TEST(ArgumentList::isSignatureValid(messyArray));
        TEST(ArgumentList::isSignatureValid(messyArray, ArgumentList::VariantSignature));
    }
    {
        cstring dictFail("a{vi}");
        TEST(!ArgumentList::isSignatureValid(dictFail));
        TEST(!ArgumentList::isSignatureValid(dictFail, ArgumentList::VariantSignature));
    }
    {
        cstring emptyStruct("()");
        TEST(!ArgumentList::isSignatureValid(emptyStruct));
        TEST(!ArgumentList::isSignatureValid(emptyStruct, ArgumentList::VariantSignature));
        cstring emptyStruct2("(())");
        TEST(!ArgumentList::isSignatureValid(emptyStruct2));
        TEST(!ArgumentList::isSignatureValid(emptyStruct2, ArgumentList::VariantSignature));
        cstring miniStruct("(t)");
        TEST(ArgumentList::isSignatureValid(miniStruct));
        TEST(ArgumentList::isSignatureValid(miniStruct, ArgumentList::VariantSignature));
        cstring badStruct("((i)");
        TEST(!ArgumentList::isSignatureValid(badStruct));
        TEST(!ArgumentList::isSignatureValid(badStruct, ArgumentList::VariantSignature));
        cstring badStruct2("(i))");
        TEST(!ArgumentList::isSignatureValid(badStruct2));
        TEST(!ArgumentList::isSignatureValid(badStruct2, ArgumentList::VariantSignature));
    }
    {
        cstring nullStr;
        cstring emptyStr("");
        TEST(!ArgumentList::isObjectPathValid(nullStr));
        TEST(!ArgumentList::isObjectPathValid(emptyStr));
        TEST(ArgumentList::isObjectPathValid(cstring("/")));
        TEST(!ArgumentList::isObjectPathValid(cstring("/abc/")));
        TEST(ArgumentList::isObjectPathValid(cstring("/abc")));
        TEST(ArgumentList::isObjectPathValid(cstring("/abc/def")));
        TEST(!ArgumentList::isObjectPathValid(cstring("/abc&def")));
        TEST(!ArgumentList::isObjectPathValid(cstring("/abc//def")));
        TEST(ArgumentList::isObjectPathValid(cstring("/aZ/0123_zAZa9_/_")));
    }
    {
        cstring maxStruct("((((((((((((((((((((((((((((((((i"
                          "))))))))))))))))))))))))))))))))");
        TEST(ArgumentList::isSignatureValid(maxStruct));
        TEST(ArgumentList::isSignatureValid(maxStruct, ArgumentList::VariantSignature));
        cstring struct33("(((((((((((((((((((((((((((((((((i" // too much nesting by one
                         ")))))))))))))))))))))))))))))))))");
        TEST(!ArgumentList::isSignatureValid(struct33));
        TEST(!ArgumentList::isSignatureValid(struct33, ArgumentList::VariantSignature));

        cstring maxArray("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(ArgumentList::isSignatureValid(maxArray));
        TEST(ArgumentList::isSignatureValid(maxArray, ArgumentList::VariantSignature));
        cstring array33("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaai");
        TEST(!ArgumentList::isSignatureValid(array33));
        TEST(!ArgumentList::isSignatureValid(array33, ArgumentList::VariantSignature));
    }
}

static bool arraysEqual(array a1, array a2)
{
    if (a1.length != a2.length) {
        return false;
    }
    for (int i = 0; i < a1.length; i++) {
        if (a1.begin[i] != a2.begin[i]) {
            return false;
        }
    }
    return true;
}

static bool stringsEqual(cstring s1, cstring s2)
{
    return arraysEqual(array(s1.begin, s1.length), array(s2.begin, s2.length));
}

static void printArray(array a)
{
    cout << "Array: ";
    for (int i = 0; i < a.length; i++) {
        cout << int(a.begin[i]) << ':';
    }
    cout << '\n';
}

static void doRoundtrip(ArgumentList arg, bool debugPrint = false)
{
    ArgumentList::ReadCursor reader = arg.beginRead();
    {
        ArgumentList::ReadCursor reader2 = arg.beginRead();
        TEST(reader2.isValid());
    }

    ArgumentList copy;
    ArgumentList::WriteCursor writer = copy.beginWrite();
    {
        ArgumentList::WriteCursor writer2 = copy.beginWrite();
        TEST(!writer2.isValid());
    }
    {
        ArgumentList::ReadCursor reader3 = copy.beginRead();
        TEST(!reader3.isValid());
    }

    bool isDone = false;
    while (!isDone) {
        TEST(writer.state() != ArgumentList::InvalidData);
        if (debugPrint) {
            cout << "Reader state: " << reader.stateString().begin << '\n';
        }

        switch(reader.state()) {
        case ArgumentList::Finished:
            writer.finish();
            isDone = true;
            break;
        case ArgumentList::NeedMoreData:
            TEST(false);
            break;
        case ArgumentList::BeginStruct:
            reader.beginStruct();
            writer.beginStruct();
            break;
        case ArgumentList::EndStruct:
            reader.endStruct();
            writer.endStruct();
            break;
        case ArgumentList::BeginVariant:
            reader.beginVariant();
            writer.beginVariant();
            break;
        case ArgumentList::EndVariant:
            reader.endVariant();
            writer.endVariant();
            break;
        case ArgumentList::BeginArray: {
            bool isEmpty;
            reader.beginArray(&isEmpty);
            writer.beginArray(isEmpty);
            break; }
        case ArgumentList::NextArrayEntry:
            if (reader.nextArrayEntry()) {
                writer.nextArrayEntry();
            } else {
                writer.endArray();
            }
            break;
        case ArgumentList::EndArray:
            reader.endArray();
            // writer.endArray(); // already done when reader.nextArrayEntry() returns false
            break;
        case ArgumentList::BeginDict: {
            bool isEmpty;
            reader.beginDict(&isEmpty);
            writer.beginDict(isEmpty);
            break; }
        case ArgumentList::NextDictEntry:
            if (reader.nextDictEntry()) {
                writer.nextDictEntry();
            } else {
                writer.endDict();
            }
            break;
        case ArgumentList::EndDict:
            reader.endDict();
            // writer.endDict(); // already done when reader.nextDictEntry() returns false
            break;
        case ArgumentList::Byte:
            writer.writeByte(reader.readByte());
            break;
        case ArgumentList::Boolean:
            writer.writeBoolean(reader.readBoolean());
            break;
        case ArgumentList::Int16:
            writer.writeInt16(reader.readInt16());
            break;
        case ArgumentList::Uint16:
            writer.writeUint16(reader.readUint16());
            break;
        case ArgumentList::Int32:
            writer.writeInt32(reader.readInt32());
            break;
        case ArgumentList::Uint32:
            writer.writeUint32(reader.readUint32());
            break;
        case ArgumentList::Int64:
            writer.writeInt64(reader.readInt64());
            break;
        case ArgumentList::Uint64:
            writer.writeUint64(reader.readUint64());
            break;
        case ArgumentList::Double:
            writer.writeDouble(reader.readDouble());
            break;
        case ArgumentList::String:
            writer.writeString(reader.readString());
            break;
        case ArgumentList::ObjectPath:
            writer.writeObjectPath(reader.readObjectPath());
            break;
        case ArgumentList::Signature:
            writer.writeSignature(reader.readSignature());
            break;
        case ArgumentList::UnixFd:
            writer.writeUnixFd(reader.readUnixFd());
            break;
        default:
            TEST(false);
            break;
        }
    }
    cstring argSignature = arg.signature();
    cstring copySignature = copy.signature();
    TEST(ArgumentList::isSignatureValid(copySignature));
    TEST(stringsEqual(argSignature, copySignature));

    array argData = arg.data();
    array copyData = copy.data();
    TEST(argData.length == copyData.length);
    if (debugPrint && !arraysEqual(argData, copyData)) {
        printArray(argData);
        printArray(copyData);
    }
    TEST(arraysEqual(argData, copyData));
}

void test_nesting()
{
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        for (int i = 0; i < 32; i++) {
            writer.beginArray(false);
            writer.nextArrayEntry();
        }
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.beginArray(false);
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        for (int i = 0; i < 32; i++) {
            writer.beginDict(false);
            writer.nextDictEntry();
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.beginStruct();
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        for (int i = 0; i < 32; i++) {
            writer.beginDict(false);
            writer.nextDictEntry();
            writer.writeInt32(i); // key, next nested dict is value
        }
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.beginArray(false);
        TEST(writer.state() == ArgumentList::InvalidData);
    }
}

struct LengthPrefixedData
{
    uint32 length;
    byte data[256];
};

void test_roundtrip()
{
    doRoundtrip(ArgumentList(cstring(""), array()));
    {
        byte data[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
        doRoundtrip(ArgumentList(cstring("i"), array(data, 4)));
        doRoundtrip(ArgumentList(cstring("yyyy"), array(data, 4)));
        doRoundtrip(ArgumentList(cstring("iy"), array(data, 5)));
        doRoundtrip(ArgumentList(cstring("iiy"), array(data, 9)));
        doRoundtrip(ArgumentList(cstring("nquy"), array(data, 9)));
        doRoundtrip(ArgumentList(cstring("unqy"), array(data, 9)));
        doRoundtrip(ArgumentList(cstring("nqy"), array(data, 5)));
        doRoundtrip(ArgumentList(cstring("qny"), array(data, 5)));
        doRoundtrip(ArgumentList(cstring("yyny"), array(data, 5)));
        doRoundtrip(ArgumentList(cstring("qyyy"), array(data, 5)));
        doRoundtrip(ArgumentList(cstring("d"), array(data, 8)));
        doRoundtrip(ArgumentList(cstring("dy"), array(data, 9)));
        doRoundtrip(ArgumentList(cstring("x"), array(data, 8)));
        doRoundtrip(ArgumentList(cstring("xy"), array(data, 9)));
        doRoundtrip(ArgumentList(cstring("t"), array(data, 8)));
        doRoundtrip(ArgumentList(cstring("ty"), array(data, 9)));
    }
    {
        LengthPrefixedData testArray = {0};
        for (int i = 0; i < 64; i++) {
            testArray.data[i] = i;
        }
        byte *testData = reinterpret_cast<byte *>(&testArray);

        testArray.length = 1;
        doRoundtrip(ArgumentList(cstring("ay"), array(testData, 5)));
        testArray.length = 4;
        doRoundtrip(ArgumentList(cstring("ai"), array(testData, 8)));
        testArray.length = 8;
        doRoundtrip(ArgumentList(cstring("ai"), array(testData, 12)));
        testArray.length = 64;
        doRoundtrip(ArgumentList(cstring("ai"), array(testData, 68)));
        doRoundtrip(ArgumentList(cstring("an"), array(testData, 68)));

        testArray.data[0] = 0; testArray.data[1] = 0; // zero out padding
        testArray.data[2] = 0; testArray.data[3] = 0;
        testArray.length = 56;
        doRoundtrip(ArgumentList(cstring("ad"), array(testData, 64)));
    }
    {
        LengthPrefixedData testString;
        for (int i = 0; i < 200; i++) {
            testString.data[i] = 'A' + i % 53; // stay in the 7-bit ASCII range
        }
        testString.data[200] = '\0';
        testString.length = 200;
        byte *testData = reinterpret_cast<byte *>(&testString);
        doRoundtrip(ArgumentList(cstring("s"), array(testData, 205)));
    }
    {
        LengthPrefixedData testDict;
        testDict.length = 2;
        testDict.data[0] = 0; testDict.data[1] = 0; // zero padding; dict entries are always 8-aligned.
        testDict.data[2] = 0; testDict.data[3] = 0;

        testDict.data[4] = 23;
        testDict.data[6] = 42;
        byte *testData = reinterpret_cast<byte *>(&testDict);
        doRoundtrip(ArgumentList(cstring("a{yy}"), array(testData, 10)));
    }
}

static void test_writerMisuse()
{
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.writeByte(1);
        writer.writeByte(2);
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.writeByte(3); // wrong, a dict contains only exactly two types
        TEST(writer.state() == ArgumentList::InvalidData);
    }
}

int main(int argc, char *argv[])
{
    test_stringValidation();
    test_nesting();
    test_roundtrip();
    test_writerMisuse();
}
