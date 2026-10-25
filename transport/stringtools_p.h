// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef STRINGTOOLS_P_H
#define STRINGTOOLS_P_H

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

#endif // STRINGTOOLS_P_H
