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

#ifndef ARGUMENTSREADER_P_H
#define ARGUMENTSREADER_P_H

#include "argumentsreader.h"
#include "arguments_p.h"

struct RestoreStateForTruncatedData
{
    uint32 savedSignaturePosition;
    uint32 savedDataPosition;
};

#ifdef HAVE_BOOST
#include <boost/container/small_vector.hpp>
#endif

class ArgumentsReader::Private
{
public:
    static inline Private *of(ArgumentsReader *reader) { return reader->d; }
    static inline const Private *of(const ArgumentsReader *reader) { return reader->d; }

    RestoreStateForTruncatedData savedStateForRetry(const ArgumentsReader *reader) const;
    void restoreStateForRetry(RestoreStateForTruncatedData restoreState);
    void replaceData(chunk data, ArgumentsReader *reader);

    const Arguments::Private *m_argsPriv = nullptr;
    cstring m_signature;
    uint32 m_signaturePosition = uint32(-1);
    chunk m_data;
    uint32 m_dataPosition = 0;
    uint32 m_nilArrayNesting = 0; // this keeps track of how many nil arrays we are in
    Error m_error;
    Nesting m_nesting;
    bool m_validateUtf8 = true;

    struct ArrayInfo
    {
        uint32 dataEnd; // one past the last data byte of the array
        uint32 containedTypeBegin; // to rewind when reading the next element
    };

    struct VariantInfo
    {
        // Using these separate fields allows this to have a size of 16 bytes instead of 24
        // (without using nonstandard "pack" pragmas with weird side effects)
        char* prevSignaturePtr;
        uint32 prevSignatureLength;
        uint32 prevSignaturePosition; // need to store the old signature and parse position.
    };

    // for structs, we don't need to know more than that we are in a struct

    struct AggregateInfo
    {
        Arguments::IoState aggregateType; // can be BeginArray, BeginDict, BeginStruct, BeginVariant
        union {
            ArrayInfo arr;
            VariantInfo var;
        };
    };

    // this keeps track of which aggregates we are currently in
#ifdef HAVE_BOOST
    boost::container::small_vector<AggregateInfo, 8> m_aggregateStack;
#else
    std::vector<AggregateInfo> m_aggregateStack;
#endif
};

// Testing support methods - their code should only occur in test binaries, not installed libraries,
// hence inline.
inline RestoreStateForTruncatedData ArgumentsReader::Private::savedStateForRetry(
                                                                const ArgumentsReader *reader) const
{
    RestoreStateForTruncatedData ret{m_signaturePosition, m_dataPosition};

    // beginVariant(), endVariant(), beginDict() and endArray() change the signature position
    // (but not the data position) before calling advanceState(). It is *that* position that needs
    // to be restored after advanceState() fails with TruncatedMessageData.
    // ### This does not necessarily work for skipFoo(), but the tests don't call this before
    // skipFoo(). If they did, we'd need some extra logic (e.g. a bool willSkip argument for this).
    if (reader->m_state == Arguments::BeginVariant) {
        ret.savedSignaturePosition = uint32(-1);
    } else if (reader->m_state == Arguments::EndVariant) {
        const Private::AggregateInfo &aggregateInfo = m_aggregateStack.back();
        const Private::VariantInfo &variantInfo = aggregateInfo.var;
        ret.savedSignaturePosition = variantInfo.prevSignaturePosition;
    } else if (reader->m_state == Arguments::BeginDict) {
        ret.savedSignaturePosition++;
    } else if (reader->m_state == Arguments::EndArray) {
        ret.savedSignaturePosition--;
    }

    return ret;
}

inline void ArgumentsReader::Private::restoreStateForRetry(RestoreStateForTruncatedData restoreState)
{
    m_signaturePosition = restoreState.savedSignaturePosition;
    m_dataPosition = restoreState.savedDataPosition;
}

inline void ArgumentsReader::Private::replaceData(chunk data, ArgumentsReader *reader)
{
    if (data.length < m_dataPosition) {
        return;
    }

    ptrdiff_t offset = data.ptr - m_data.ptr;

    // fix up variant signature addresses occurring on the aggregate stack pointing into m_data;
    // don't touch the original (= call parameter, not variant) signature, which does not point into m_data.
    bool isMainSignature = true;
    for (Private::AggregateInfo &aggregate : m_aggregateStack) {
        if (aggregate.aggregateType == Arguments::BeginVariant) {
            if (isMainSignature) {
                isMainSignature = false;
            } else {
                aggregate.var.prevSignaturePtr += offset;
            }
        }
    }
    if (!isMainSignature) {
        m_signature.ptr += offset;
    }

    m_data = data;
    if (reader->m_state == Arguments::InvalidData && m_error.code() == Error::TruncatedMessageData) {
        reader->m_state = Arguments::AnyData; // ### get past the m_state check in advanceState()
        reader->advanceState();
    }
}
#endif // ARGUMENTSREADER_P_H
