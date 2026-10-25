// SPDX-FileCopyrightText: 2013 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#ifndef PLATFORM_P_H
#define PLATFORM_P_H

// Note about invalid descriptors: On Unix they are -1 signed, on Windows they are
// ~0 unsigned. Same bit pattern, not directly interchangeable in use.

#ifdef _WIN32
// this is ugly, but including all of winsock2.h in lots of headers is also ugly...
#ifdef _WIN64
typedef unsigned long long int FileDescriptor;
#else
typedef unsigned int FileDescriptor;
#endif
#else
// So far, Windows is the only supported platform where a file descriptor isn't just a (signed) int.
typedef int FileDescriptor;
#endif

enum InvalidFileDescriptorEnum : FileDescriptor {
#ifdef _WIN32
    InvalidFileDescriptor = ~ FileDescriptor(0)
#else
    InvalidFileDescriptor = -1
#endif
};

static inline bool isValidFileDescriptor(FileDescriptor fd)
{
#ifdef _WIN32
    return fd != InvalidFileDescriptor;
#else
    return fd >= 0;
#endif
}

#endif // PLATFORM_P_H
