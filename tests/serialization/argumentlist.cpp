#include "argumentlist.h"

#include "../testutil.h"

#include <cstring>
#include <iostream>

using namespace std;

void test_stringValidation()
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

    // TODO test nesting limits in signatures (might be best to generate strings)
}

void test_roundtrip()
{
    cstring signature(""); // TODO
    array data; // TODO

    ArgumentList arg(signature, data);
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
        cerr << "Reader state: " << reader.state() << endl;

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

    writer.finish();

    // TODO compare the binary output of writer with the binary input of reader
}

int main(int argc, char *argv[])
{
    test_stringValidation();
    test_roundtrip();
}
