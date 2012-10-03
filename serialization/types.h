#ifndef TYPES_H
#define TYPES_H

typedef unsigned char byte;
typedef short int int16;
typedef unsigned short int uint16;
typedef int int32;
typedef unsigned int uint32;
typedef long long int int64;
typedef unsigned long long int uint64;

struct array
{
    array() : begin(0), length(0) {}
    array(byte *b, int l) : begin(b), length(l) {}
    array(char *b, int l) : begin(reinterpret_cast<byte *>(b)), length(l) {}
    array(const char *b, int l) : begin(reinterpret_cast<byte *>(const_cast<char *>(b))), length(l) {}
    array(const char *b);
    byte *begin;
    int length;
};

#endif // TYPES_H
