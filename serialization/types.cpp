#include "types.h"

#include <cstring>

// for now, this .cpp file mainly exists to avoid including cstring in types.h that is supposed
// to include just basic typey stuff.

cstring::cstring(const char *b)
   : begin(reinterpret_cast<byte *>(const_cast<char *>(b))),
     length(strlen(b))
{}
