/*
   Copyright (C) 2013 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef ARGUMENTS_H
#define ARGUMENTS_H

#include "export.h"
#include "types.h"

#include <cassert>
#include <string>
#include <vector>

class Error;
class Message;
class MessagePrivate;

//#define WITH_DICT_ENTRY

class DFERRY_EXPORT Arguments
{
public:
    class Reader;
    class Writer;

    enum SignatureType
    {
        MethodSignature = 0,
        VariantSignature
    };

    enum
    {
        MaxSignatureLength = 255,
        MaxArrayLength = 1 << 26, // 64 MiB
        MaxMessageLength = 1 << 27 // 128 MiB
        // MaxMessageLength applies to a full Message (and it IS checked in Message when the message is
        // complete), which also implies a max size for Arguments. Instead of working out the exact minimum
        // size of the header part of a Message (which depends on too many variables), just allow the max
        // Message length as max Arguments length.
        // The way the limit is defined in the spec seems tied a bit too closely to the design of libdbus-1...
    };

    enum IoState : byte
    {
        // "exceptional" states
        NotStarted = 0,
        Finished,
        NeedMoreData, // recoverable by adding data; should only happen when parsing the not length-prefixed variable message header
        InvalidData, // non-recoverable
        // Writer states when the next type is still open (not iterating in an array or dict)
        // ### it is inconsistent to have DictKey, but nothing for other constraints. The name AnyData is
        //     also weird. Remove DictKey and call AnyData InputData?
        AnyData, // occurs in Writer when you are free to add any type
        DictKey, // occurs in Writer when the next type must be suitable for a dict key -
                 // a simple string or numeric type.

        // the following occur in Reader, and in Writer when in the second or higher iteration
        // of an array or dict where the types must match the first iteration (except inside variants).

        // states pertaining to aggregates
        BeginArray,
        EndArray,
        BeginDict,
        EndDict,
        BeginStruct, // 10
        EndStruct,
        BeginVariant,
        EndVariant,
        // the next element is plain data
        Boolean,
        Byte,
        Int16,
        Uint16,
        Int32,
        Uint32,
        Int64, // 20
        Uint64,
        Double,
        String,
        ObjectPath,
        Signature,
        UnixFd,
#ifdef WITH_DICT_ENTRY
        BeginDictEntry,
        EndDictEntry,
#endif
        LastState
    };

    // Constructs an empty argument list
    Arguments();

    // Constructs an argument list to deserialize data in @p data with signature @p signature;
    // if memOwnership is non-null, this means that signature and data's memory is contained in
    // a malloc()ed block starting at memOwnership, and ~Arguments will free it.
    // Otherwise, the instance assumes that @p signature and @p data live in "borrowed" memory and
    // you need to make sure that the memory lives as long as the Arguments.
    // (A notable user of this is Message - you can only get a const ref to its internal Arguments
    //  so you need to copy to take the Arguments away from the Message, which copies out of the
    //  borrowed memory into heap memory so the copy is safe)
    // The copy constructor and assignment operator will always copy the data, so copying is safe
    // regarding memory correctness but has a significant performance impact.
    Arguments(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped = false);

    // Same thing as above, with file descriptors added
    Arguments(byte *memOwnership, cstring signature, chunk data,
              std::vector<int> fileDescriptors, bool isByteSwapped = false);

    // use these wherever possible if you care at all about efficiency!!
    Arguments(Arguments &&other);
    Arguments &operator=(Arguments &&other);

    // copying needs special treatment due to the d-pointer
    Arguments(const Arguments &other);
    Arguments &operator=(const Arguments &other);

    ~Arguments();

    // error (if any) propagates to Message, so it is still available later
    Error error() const;

    std::string prettyPrint() const;

    cstring signature() const;
    chunk data() const;
    const std::vector<int> &fileDescriptors() const;
    bool isByteSwapped() const;

    static bool isStringValid(cstring string);
    static bool isObjectPathValid(cstring objectPath);
    static bool isObjectPathElementValid(cstring pathElement);
    static bool isSignatureValid(cstring signature, SignatureType type = MethodSignature);

    static void copyOneElement(Reader *reader, Writer *writer);

#include "argumentsreader_p.h"
#include "argumentsbcreader_p.h"

#include "argumentswriter_p.h"

    class Private;

private:
    Private *d;
};

#endif // ARGUMENTS_H
