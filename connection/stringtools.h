#ifndef STRINGTOOLS_H
#define STRINGTOOLS_H

#include <string>
#include <vector>

std::vector<std::string> split(const std::string &s, char delimiter);
std::string hexEncode(const std::string &s);

#endif // STRINGTOOLS_H
