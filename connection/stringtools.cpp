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

vector<string> split(const string &s, char delimiter)
{
    vector<string> ret;
    stringstream ss(s);
    string part;
    while (getline(ss, part, delimiter)) {
        ret.push_back(part);
    }
    return ret;
}

string hexEncode(const string &s)
{
    stringstream ss;
    for (int i = 0; i < s.length(); i++) {
        const char c = s[i];
        ss << std::hex << int(c >> 4) << int(c & 0xf);
    }
    return ss.str();
}
