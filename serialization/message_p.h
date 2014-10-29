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

#include "argumentlist.h"
#include "iconnectionclient.h"

class VarHeaderStorage {
public:
    bool hasHeader(Message::VariableHeader header) const;

    bool hasStringHeader(Message::VariableHeader header) const;
    std::string stringHeader(Message::VariableHeader header) const;
    void setStringHeader(Message::VariableHeader header, const std::string &value);
    void clearStringHeader(Message::VariableHeader header);

    bool hasIntHeader(Message::VariableHeader header) const;
    uint32 intHeader(Message::VariableHeader header) const;
    void setIntHeader(Message::VariableHeader header, uint32 value);
    void clearIntHeader(Message::VariableHeader header);

    // for use during header deserialization: returns false if a header occurs twice,
    // but does not check if the given header is of the right type (int / string).
    bool setIntHeader_deser(Message::VariableHeader header, uint32 value);
    bool setStringHeader_deser(Message::VariableHeader header, std::string value);

    static const int s_stringHeaderCount = 7;
    static const int s_intHeaderCount = 2;
    std::string m_stringHeaders[s_stringHeaderCount];
    uint32 m_intHeaders[s_intHeaderCount];
    uint32 m_headerPresenceBitmap = 0;
};

class MessagePrivate : public IConnectionClient
{
public:
    MessagePrivate(Message *parent);

    virtual void notifyConnectionReadyRead() override;
    virtual void notifyConnectionReadyWrite() override;

    bool requiredHeadersPresent() const;
    bool deserializeFixedHeaders();
    bool deserializeVariableHeaders();
    bool fillOutBuffer();
    void serializeFixedHeaders();
    void serializeVariableHeaders(ArgumentList *headerArgs);

    void notifyCompletionClient();

    Message *m_message;
    // there is no explicit dirty flag; the buffer is simply cleared when dirtying any of the data below.
    std::vector<byte> m_buffer;

    bool m_isByteSwapped;
    enum {
        Empty,
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
    uint32 m_headerLength;
    uint32 m_headerPadding;
    uint32 m_bodyLength;
    uint32 m_serial;

    ArgumentList m_mainArguments;

    VarHeaderStorage m_varHeaders;

    ICompletionClient *m_completionClient;
};

#endif // MESSAGE_P_H
