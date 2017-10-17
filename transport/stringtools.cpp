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

#include "stringtools.h"

#include <sstream>

using namespace std;

vector<string> split(const string &s, char delimiter, bool keepEmptyParts)
{
    vector<string> ret;
    stringstream ss(s);
    string part;
    while (getline(ss, part, delimiter)) {
        if (keepEmptyParts || !part.empty()) {
            ret.push_back(part);
        }
    }
    return ret;
}

#ifndef DFERRY_SERDES_ONLY
#include "sha1.c"

string hexEncode(const string &s)
{
    stringstream ss;
    for (size_t i = 0; i < s.length(); i++) {
        const byte b = static_cast<byte>(s[i]);
        ss << std::hex << uint(b >> 4) << uint(b & 0xf);
    }
    return ss.str();
}

string sha1Hex(const string &s)
{
    sha1nfo sha;
    sha1_init(&sha);
    sha1_write(&sha, s.c_str(), s.length());
    // SHA-1 produces a 160 bits result, which is 20 bytes
    string shaResult(reinterpret_cast<char *>(sha1_result(&sha)), 20);
    return hexEncode(shaResult);
}
#endif
