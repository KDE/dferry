#include "argumentlist.h"

#include <cassert>
#include <cstring>

void test_roundtrip()
{
    static const char *signature = ""; // TODO
    array signatureArray(const_cast<char *>(signature), strlen(signature) + 1); // TODO get rid of the +1?

    static const char *contents = ""; // TODO
    static const int contentsLen = 1; // TODO
    array contentsArray(const_cast<char *>(contents), contentsLen);

    ArgumentList arg(signatureArray, contentsArray);
    ArgumentList::ReadCursor reader = arg.beginRead();
    {
        ArgumentList::ReadCursor reader2 = arg.beginRead();
        assert(reader2.isValid());
    }

    ArgumentList copy;
    ArgumentList::WriteCursor writer = copy.beginWrite();
    {
        ArgumentList::WriteCursor writer2 = copy.beginWrite();
        assert(!writer2.isValid());
    }
    {
        ArgumentList::ReadCursor reader3 = copy.beginRead();
        assert(!reader3.isValid());
    }

    bool isDone = false;
    while (!isDone) {
        assert(writer.state() != ArgumentList::InvalidData);

        switch(reader.state()) {
        case ArgumentList::Finished:
            writer.finish();
            isDone = true;
            break;
        case ArgumentList::NeedMoreData:
            assert(false);
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
            assert(false);
            break;
        }
    }

    // TODO compare the binary output of writer with the binary input of reader
}

int main(int argc, char *argv[])
{
}