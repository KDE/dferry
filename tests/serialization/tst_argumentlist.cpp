#include "argumentlist.h"

#include "../testutil.h"

#include <algorithm>
#include <cstring>
#include <iostream>

using namespace std;

// Handy helpers

static void printArray(array a)
{
    cout << "Array: ";
    for (int i = 0; i < a.length; i++) {
        cout << int(a.begin[i]) << '|';
    }
    cout << '\n';
}

static bool arraysEqual(array a1, array a2)
{
    if (a1.length != a2.length) {
        cout << "Different lengths.\n";
        printArray(a1);
        printArray(a2);
        return false;
    }
    for (int i = 0; i < a1.length; i++) {
        if (a1.begin[i] != a2.begin[i]) {
            cout << "Different content.\n";
            printArray(a1);
            printArray(a2);
            return false;
        }
    }
    return true;
}

static bool stringsEqual(cstring s1, cstring s2)
{
    return arraysEqual(array(s1.begin, s1.length), array(s2.begin, s2.length));
}

static void doRoundtrip(ArgumentList arg_in, bool skipNextEntryAtArrayStart, int dataIncrement, bool debugPrint)
{
    cstring signature = arg_in.signature();
    array data = arg_in.data();
    array shortData;

    ArgumentList arg(signature, shortData);

    ArgumentList::ReadCursor reader = arg.beginRead();

    ArgumentList copy;
    ArgumentList::WriteCursor writer = copy.beginWrite();

    bool isDone = false;
    bool isFirstEntry = false;

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
        case ArgumentList::NeedMoreData: {
            TEST(shortData.length < data.length);
            // reallocate shortData to test that ReadCursor can handle the data moving around - and
            // allocate the new one before destroying the old one to make sure that the pointer differs
            array oldData = shortData;
            shortData.length = std::min(shortData.length + dataIncrement, data.length);
            shortData.begin = reinterpret_cast<byte *>(malloc(shortData.length));
            for (int i = 0; i < shortData.length; i++) {
                shortData.begin[i] = data.begin[i];
            }
            // clobber it to provoke errors that only valgrind might find otherwise
            for (int i = 0; i < oldData.length; i++) {
                oldData.begin[i] = '\xff';
            }
            if (oldData.begin) {
                free(oldData.begin);
            }
            reader.replaceData(shortData);
            break; }
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
            isFirstEntry = true;
            bool isEmpty;
            reader.beginArray(&isEmpty);
            writer.beginArray(isEmpty);
            break; }
        case ArgumentList::NextArrayEntry:
            if (reader.nextArrayEntry()) {
                if (isFirstEntry && skipNextEntryAtArrayStart) {
                    isFirstEntry = false;
                } else {
                    writer.nextArrayEntry();
                }
            }
            break;
        case ArgumentList::EndArray:
            reader.endArray();
            writer.endArray();
            break;
        case ArgumentList::BeginDict: {
            isFirstEntry = true;
            bool isEmpty;
            reader.beginDict(&isEmpty);
            writer.beginDict(isEmpty);
            break; }
        case ArgumentList::NextDictEntry:
            if (reader.nextDictEntry()) {
                if (isFirstEntry && skipNextEntryAtArrayStart) {
                    isFirstEntry = false;
                } else {
                    writer.nextDictEntry();
                }
            }
            break;
        case ArgumentList::EndDict:
            reader.endDict();
            writer.endDict();
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
        case ArgumentList::String: {
            cstring s = reader.readString();
            TEST(ArgumentList::isStringValid(s));
            writer.writeString(s);
            break; }
        case ArgumentList::ObjectPath: {
            cstring objectPath = reader.readObjectPath();
            TEST(ArgumentList::isObjectPathValid(objectPath));
            writer.writeObjectPath(objectPath);
            break; }
        case ArgumentList::Signature: {
            cstring signature = reader.readSignature();
            TEST(ArgumentList::isSignatureValid(signature));
            writer.writeSignature(signature);
            break; }
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

    // TODO when it's wired up between ReadCursor and ArgumentList: array argData = arg.data();
    array argData = arg_in.data();

    array copyData = copy.data();
    TEST(argData.length == copyData.length);
    if (debugPrint && !arraysEqual(argData, copyData)) {
        printArray(argData);
        printArray(copyData);
    }
    TEST(arraysEqual(argData, copyData));

    if (shortData.begin) {
        free(shortData.begin);
    }
    free(copySignature.begin);
    free(copyData.begin);
}

static void doRoundtrip(ArgumentList arg, bool debugPrint = false)
{
    int maxIncrement = arg.data().length;
    for (int i = 1; i <= maxIncrement; i++) {
        doRoundtrip(arg, false, i, debugPrint);
        doRoundtrip(arg, true, i, debugPrint);
    }
}



// Tests proper



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

static void test_readerWriterExclusion()
{
    ArgumentList arg;
    {
        ArgumentList::ReadCursor reader1 = arg.beginRead();
        {
            ArgumentList::ReadCursor reader2 = arg.beginRead();
            TEST(reader2.isValid());
        }
        {
            ArgumentList::WriteCursor writer1 = arg.beginWrite();
            TEST(!writer1.isValid());
        }
    }
    {
        ArgumentList::ReadCursor reader3 = arg.beginRead();
        TEST(reader3.isValid());
    }
    {
        ArgumentList::WriteCursor writer2 = arg.beginWrite();
        TEST(writer2.isValid());
        {
            ArgumentList::ReadCursor reader4 = arg.beginRead();
            TEST(!reader4.isValid());
        }
        {
            ArgumentList::ReadCursor writer3 = arg.beginRead();
            TEST(!writer3.isValid());
        }
    }
    {
        ArgumentList::WriteCursor writer4 = arg.beginWrite();
        TEST(writer4.isValid());
    }
}

static void test_nesting()
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
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        for (int i = 0; i < 64; i++) {
            writer.beginVariant();
        }
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.beginVariant();
        TEST(writer.state() == ArgumentList::InvalidData);
    }
}

struct LengthPrefixedData
{
    uint32 length;
    byte data[256];
};

static void test_roundtrip()
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
    {
        byte testData[36] = {
            5, // variant signature length
            '(', 'y', 'g', 'd', ')', '\0', // signature: struct of: byte, signature (easiest because
                                           //   its length prefix is byte order independent), double
            0,      // pad to 8-byte boundary for struct
            23,     // the byte
            6, 'i', 'a', '{', 'i', 'v', '}', '\0', // the signature
            0, 0, 0, 0, 0, 0, 0,    // padding to 24 bytes (next 8-byte boundary)
            1, 2, 3, 4, 5, 6, 7, 8, // the double
            20, 21, 22, 23 // the int (not part of the variant)
        };
        doRoundtrip(ArgumentList(cstring("vi"), array(testData, 36)));
    }
}

static void test_writerMisuse()
{
    // Array
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginArray(false);
        writer.endArray(); // wrong,  must contain exactly one type
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginArray(false);
        writer.writeByte(1); // in WriteCursor, calling nextArrayEntry() after beginArray() is optional
        writer.endArray();
        TEST(writer.state() != ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginArray(false);
        writer.nextArrayEntry();
        writer.endArray(); // wrong, must contain exactly one type
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginArray(false);
        writer.nextArrayEntry();
        writer.writeByte(1);
        writer.writeByte(2);  // wrong, must contain exactly one type
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    // Dict
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginDict(false);
        writer.endDict(); // wrong, must contain exactly two types
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.writeByte(1);
        writer.endDict(); // wrong, a dict must contain exactly two types
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginDict(false);
        writer.writeByte(1); // in WriteCursor, calling nextDictEntry() after beginDict() is optional
        writer.writeByte(2);
        writer.endDict();
        TEST(writer.state() != ArgumentList::InvalidData);
    }
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
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginDict(false);
        writer.nextDictEntry();
        writer.beginVariant(); // wrong, key type must be basic
        TEST(writer.state() == ArgumentList::InvalidData);
    }
    // Variant
    {
        // this and the next are a baseline to make sure that the following test fails for a good reason
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginVariant();
        writer.writeByte(1);
        writer.endVariant();
        TEST(writer.state() != ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginVariant();
        writer.endVariant();
        TEST(writer.state() != ArgumentList::InvalidData);
    }
    {
        ArgumentList arg;
        ArgumentList::WriteCursor writer = arg.beginWrite();
        writer.beginVariant();
        writer.writeByte(1);
        writer.writeByte(2); // wrong, a variant may contain only one or zero single complete types
        TEST(writer.state() == ArgumentList::InvalidData);
    }
}

void addSomeVariantStuff(ArgumentList::WriteCursor *writer)
{
    writer->beginVariant();
        writer->beginVariant();
            writer->beginVariant();
                writer->beginStruct();
                    writer->writeString(cstring("Smoerebroed smoerebroed"));
                    writer->beginStruct();
                        writer->writeString(cstring("Bork bork bork"));
                        writer->beginVariant();
                            writer->beginStruct();
                                writer->writeString(cstring("Quite nesty"));
                                writer->writeObjectPath(cstring("/path/to/object"));
                                writer->writeUint64(234234234);
                                writer->writeByte(2);
                                writer->writeUint64(234234223434);
                                writer->writeUint16(34);
                            writer->endStruct();
                        writer->endVariant();
                        writer->beginStruct();
                            writer->writeByte(34);
                        writer->endStruct();
                    writer->endStruct();
                    writer->writeString(cstring("Another string"));
                writer->endStruct();
            writer->endVariant();
        writer->endVariant();
    writer->endVariant();
}

static void test_complicated()
{
    ArgumentList arg;
    {
        ArgumentList::WriteCursor writer = arg.beginWrite();
        // NeedMoreData-related bugs are less dangerous inside arrays, so we try to provoke one here;
        // the reason for arrays preventing failures is that they have a length prefix which enables
        // and encourages pre-fetching all the array's data before processing *anything* inside the
        // array. therefore no NeedMoreData state happens while really deserializing the array's
        // contents. but we exactly want NeedMoreData while in the middle of deserializing something
        // meaty, specifically variants. see ReadCursor::replaceData().
        addSomeVariantStuff(&writer);

        writer.writeInt64(234234);
        writer.beginVariant();
            writer.beginDict(false);
                writer.writeByte(23);
                writer.beginVariant();
                    writer.writeString(cstring("twenty-three"));
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(83);
                writer.beginVariant();
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(234);
                writer.beginVariant();
                    writer.beginArray(false);
                        writer.writeUint16(234);
                    writer.nextArrayEntry();
                        writer.writeUint16(234);
                    writer.nextArrayEntry();
                        writer.writeUint16(234);
                    writer.endArray();
                writer.endVariant();
            writer.nextDictEntry();
                writer.writeByte(25);
                writer.beginVariant();
                    addSomeVariantStuff(&writer);
                writer.endVariant();
            writer.endDict();
        writer.endVariant();
        writer.writeString("Hello D-Bus!");
        writer.beginArray(false);
            writer.writeDouble(1.567898);
        writer.nextArrayEntry();
            writer.writeDouble(1.523428);
        writer.nextArrayEntry();
            writer.writeDouble(1.621133);
        writer.nextArrayEntry();
            writer.writeDouble(1.982342);
        writer.endArray();
        TEST(writer.state() != ArgumentList::InvalidData);
        writer.finish();
        TEST(writer.state() != ArgumentList::InvalidData);
    }
    doRoundtrip(arg);
}

int main(int argc, char *argv[])
{
    test_stringValidation();
    test_readerWriterExclusion();
    test_nesting();
    test_roundtrip();
    test_writerMisuse();
    test_complicated();
    // TODO many more misuse tests for WriteCursor and maybe some for ReadCursor
    std::cout << "Passed!\n";
}
