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

#ifdef HAVE_BOOST
#include <boost/smart_ptr/local_shared_ptr.hpp>
#else
#include <memory>
#endif

class ArgumentsReader;
class ArgumentsWriter;
class Error;
class Message;
class MessagePrivate;

//#define WITH_DICT_ENTRY

class DFERRY_EXPORT Arguments
{
public:
    /// Type of a type signature
    enum SignatureType
    {
        MethodSignature = 0, ///< Signature of a method, may contain 0 or more single complete types
                             // (TODO link to DBus spec?)
        VariantSignature ///< Signature of a variant, must contain exactly one single complete type
    };

    static constexpr uint32 MaxSignatureLength = 255; ///< Maximum length of a DBus type signature according to spec
    static constexpr uint32 MaxArrayLength = 1 << 26; ///< Maximum length of a DBus array according to spec (64 MiB)
    static constexpr uint32 MaxMessageLength = 1 << 27; ///< Maximum length of a DBus message according to spec (128 MiB)
    // MaxMessageLength applies to a full Message (and it IS checked in Message when the message is
    // complete), which also implies a max size for Arguments. Instead of working out the exact minimum
    // size of the header part of a Message (which depends on too many variables), just allow the max
    // Message length as max Arguments length.
    // The way the limit is defined in the spec seems tied a bit too closely to the design of
    // libdbus-1, which conflates Arguments and Message.

    /// State of serialization or deserialization
    enum IoState : byte
    {
        // "exceptional" states
        NotStarted = 0, ///< De/serialization not started
        Finished, ///< De/serialization finished (usually end of data, but *can* be due to an error)
        // Writer states when the next type is still open (not iterating in an array or dict)
        // ### it is inconsistent to have DictKey, but nothing for other constraints. The name AnyData is
        //     also weird. Remove DictKey and call AnyData InputData?
        AnyData, ///< Occurs in ArgumentsWriter when any type can be written
        DictKey, ///< Occurs in ArgumentsWriter when the next type must be suitable for a dict key -
                 ///  a simple string or numeric type.

        // the following occur in Reader, and in Writer when in the second or higher iteration
        // of an array or dict where the types must match the first iteration (except inside variants).

        // states pertaining to aggregates
        BeginArray, ///< Beginning of an array
        EndArray, ///< End of an array
        BeginDict, ///< Beginning of a dict
        EndDict, ///< End of a dict
        BeginStruct, ///< Beginning of a struct
        EndStruct, ///< End of a struct
        BeginVariant, ///< Beginning of a variant
        EndVariant, ///< End of a variant
        // the next element is plain data
        Boolean, ///< Boolean value
        Byte, ///< Byte value (8 bit unsigned integer)
        Int16, ///< 16 bit signed integer value
        Uint16, ///< 16 bit unsigned integer value
        Int32, ///< 32 bit signed integer value
        Uint32, ///< 32 bit unsigned integer value
        Int64, ///< 64 bit signed integer value
        Uint64, ///< 64 bit unsigned integer value
        Double, ///< Double-precision floating point value
        String, ///< UTF-8 string value
        ObjectPath, ///< DBus object path string value
        Signature, ///< DBus type signature string value
        UnixFd, ///< Unix file descriptor value
#ifdef WITH_DICT_ENTRY
        BeginDictEntry,
        EndDictEntry,
#endif
        LastState ///< The last known state value, for internal use. Value may change.
    };

    /// Options for type signature preprocessing into FerCode
    enum class FerEncodeOptions
    {
        None = 0, ///< Normal behavior
        ElideStructs ///< Elide structs. There will be no BeginStruct and EndStruct states and no need to
                     ///  call beginStruct() or endStruct(). This is faster than having them and takes less
                     ///  code to handle, but can be confusing.
    };

    Arguments();

    // TODO probably mention this in copy and assignment ctor / operator.
    // The copy constructor and assignment operator will always copy the data, so copying is safe
    // regarding memory correctness but has a significant performance impact.
    Arguments(byte *memOwnership, cstring signature, chunk data, bool isByteSwapped = false);

    // Same thing as above, with file descriptors added
    Arguments(byte *memOwnership, cstring signature, chunk data,
              std::vector<int> fileDescriptors, bool isByteSwapped = false);

    Arguments(Arguments &&other);
    Arguments &operator=(Arguments &&other);

    Arguments(const Arguments &other);
    Arguments &operator=(const Arguments &other);

    ~Arguments();

    Error error() const;

    std::string prettyPrint() const;

    cstring signature() const;
    chunk data() const;
    const std::vector<int> &fileDescriptors() const;
    bool isByteSwapped() const;

    static bool isStringValid(cstring string, bool checkUtf8 = true);
    static bool isObjectPathValid(cstring objectPath);
    static bool isObjectPathElementValid(cstring pathElement);
    static bool isSignatureValid(cstring signature, SignatureType type = MethodSignature);

    /// \cond PRIVATE (this mainly exists to support unit tests)
    static void copyOneElement(ArgumentsReader *reader, ArgumentsWriter *writer);
    /// \endcond

    class Private;

private:
    Private *d;
};

#endif // ARGUMENTS_H
