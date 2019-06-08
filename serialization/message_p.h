/*
   Copyright (C) 2014 Andreas Hartmetz <ahartmetz@gmail.com>

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

#ifndef MESSAGE_P_H
#define MESSAGE_P_H

#include "message.h"

#include "arguments.h"
#include "error.h"
#include "itransportlistener.h"

#include <type_traits>

class ICompletionListener;

class VarHeaderStorage {
public:
    VarHeaderStorage();
    VarHeaderStorage(const VarHeaderStorage &other);
    ~VarHeaderStorage();

    bool hasHeader(Message::VariableHeader header) const;

    bool hasStringHeader(Message::VariableHeader header) const;
    std::string stringHeader(Message::VariableHeader header) const;
    cstring stringHeaderRaw(Message::VariableHeader header);
    void setStringHeader(Message::VariableHeader header, const std::string &value);
    void clearStringHeader(Message::VariableHeader header);

    bool hasIntHeader(Message::VariableHeader header) const;
    uint32 intHeader(Message::VariableHeader header) const;
    void setIntHeader(Message::VariableHeader header, uint32 value);
    void clearIntHeader(Message::VariableHeader header);

    // for use during header deserialization: returns false if a header occurs twice,
    // but does not check if the given header is of the right type (int / string).
    bool setIntHeader_deser(Message::VariableHeader header, uint32 value);
    bool setStringHeader_deser(Message::VariableHeader header, cstring value);

    const std::string *stringHeaders() const
    {
        return reinterpret_cast<const std::string *>(m_stringStorage);
    }
    std::string *stringHeaders()
    {
        return reinterpret_cast<std::string *>(m_stringStorage);
    }

    static const int s_stringHeaderCount = 7;
    static const int s_intHeaderCount = 2;

    // Uninitialized storage for strings, to avoid con/destructing strings we'd never touch otherwise.
    std::aligned_storage<sizeof(std::string)>::type m_stringStorage[VarHeaderStorage::s_stringHeaderCount];
    uint32 m_intHeaders[s_intHeaderCount];
    uint32 m_headerPresenceBitmap = 0;
};

class MessagePrivate : public ITransportListener
{
public:
    static MessagePrivate *get(Message *m) { return m->d; }

    MessagePrivate(Message *parent);
    MessagePrivate(const MessagePrivate &other, Message *parent);
    ~MessagePrivate() override;

    void handleTransportCanRead() override;
    void handleTransportCanWrite() override;

    // ITransport is non-public API, so these make no sense in the public interface
    void receive(ITransport *transport); // fills in this message from transport
    void send(ITransport *transport); // sends this message over transport
    // for receive or send completion (it should be clear which because receiving and sending can't
    // happen simultaneously)
    void setCompletionListener(ICompletionListener *listener);

    bool requiredHeadersPresent();
    Error checkRequiredHeaders() const;
    bool deserializeFixedHeaders();
    bool deserializeVariableHeaders();
    bool serialize();
    void serializeFixedHeaders();
    Arguments serializeVariableHeaders();

    void clearBuffer();
    void reserveBuffer(uint32 newSize);

    void notifyCompletionListener();

    Message *m_message;
    chunk m_buffer;
    uint32 m_bufferPos;
    std::vector<int> m_fileDescriptors;

    bool m_isByteSwapped;
    enum { // ### we don't have an error state, the need hasn't arisen yet. strange!
        Empty = 0,
        Serialized,
        Deserialized,
        LastSteadyState = Deserialized,
        Serializing,
        Deserializing
    } m_state;
    Message::Type m_messageType;
    enum {
        NoReplyExpectedFlag = 1,
        NoAutoStartFlag = 2
    };
    byte m_flags;
    byte m_protocolVersion;
    bool m_dirty : 1;
    uint32 m_headerLength;
    uint32 m_headerPadding;
    uint32 m_bodyLength;
    uint32 m_serial;

    Error m_error;

    Arguments m_mainArguments;

    VarHeaderStorage m_varHeaders;

    ICompletionListener *m_completionListener;
};

#endif // MESSAGE_P_H
