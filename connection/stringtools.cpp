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
        ss << std::hex << int(c >> 4) << int (c & 0xf);
    }
    return ss.str();
}
