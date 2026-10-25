// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "stringtools_p.h"

#include <sstream>

std::vector<std::string> split(const std::string &s, char delimiter, bool keepEmptyParts)
{
    std::vector<std::string> ret;
    std::stringstream ss(s);
    std::string part;
    while (getline(ss, part, delimiter)) {
        if (keepEmptyParts || !part.empty()) {
            ret.push_back(part);
        }
    }
    return ret;
}

#ifndef DFERRY_SERDES_ONLY
#include "sha1.c"

std::string hexEncode(const std::string &s)
{
    std::stringstream ss;
    for (size_t i = 0; i < s.length(); i++) {
        const byte b = static_cast<byte>(s[i]);
        ss << std::hex << uint(b >> 4) << uint(b & 0xf);
    }
    return ss.str();
}

std::string sha1Hex(const std::string &s)
{
    sha1nfo sha;
    sha1_init(&sha);
    sha1_write(&sha, s.c_str(), s.length());
    // SHA-1 produces a 160 bits result, which is 20 bytes
    const std::string shaResult(reinterpret_cast<char *>(sha1_result(&sha)), 20);
    return hexEncode(shaResult);
}
#endif
