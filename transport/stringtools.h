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

#ifndef STRINGTOOLS_H
#define STRINGTOOLS_H

#include "types.h"

#include <sstream>
#include <string>
#include <vector>

// export is for dferryclient library...
std::vector<std::string> DFERRY_EXPORT split(const std::string &s, char delimiter, bool keepEmptyParts = true);
#ifndef DFERRY_SERDES_ONLY
std::string hexEncode(const std::string &s);
std::string sha1Hex(const std::string &s);
#endif

inline std::string toStdString(cstring cstr)
{
    return std::string(cstr.ptr, cstr.length);
}

// In C++11, std::to_string doesn't exist yet... so use this instead
template <typename T>
std::string dfToString(T value)
{
    std::stringstream ss;
    ss.imbue(std::locale::classic());
    ss << value;
    return ss.str();
}

// kind of like std::stoi (note: available in C+11) but without exceptions
template <typename T>
static bool dfFromString(const std::string &s, T *t)
{
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    is >> *t;
    return !is.fail() && is.eof();
}

#endif // STRINGTOOLS_H
