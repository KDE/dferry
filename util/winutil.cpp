/*
   Copyright (C) 2015 Andreas Hartmetz <ahartmetz@gmail.com>

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

#include "winutil.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#endif


using namespace std;

string fetchWindowsSid()
{
    // Since this code is adapted from libdbus, keep the processId parameter which is only used
    // by the server, in case we need it later. The fixed value should let currently dead code
    // get optimized out.
    const DWORD processId = 0;
    const HANDLE processHandle = processId == 0 ? GetCurrentProcess() :
                                 OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

    bool ok = true;

    HANDLE processToken = INVALID_HANDLE_VALUE;
    if (ok && !OpenProcessToken(processHandle, TOKEN_QUERY, &processToken)) {
        ok = false;
        cerr << "OpenProcessToken failed " << GetLastError() << '\n';
    }

    PSID psid;
    if (ok) {
        DWORD n;
        SetLastError(0);
        GetTokenInformation(processToken, TokenUser, nullptr, 0, &n);
        TOKEN_USER *token_user = static_cast<TOKEN_USER *>(alloca(n));
        // cerr << "GetTokenInformation to get length: length is " << n
        //     << " and GetLastError() returns " << GetLastError() << ".\n";
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !token_user) {
            n = 0;
        }
        if (GetTokenInformation(processToken, TokenUser, token_user, n, &n)) {
            psid = token_user->User.Sid;
        } else {
            ok = false;
            cerr << "GetTokenInformation failed " << GetLastError() << '\n';
        }
    }

    if (ok && !IsValidSid(psid)) {
        ok = false;
        cerr << "IsValidSid() says no\n";
    }

    string ret;
    if (ok) {
        char *sidChar = nullptr;
        if (ConvertSidToStringSidA(psid, &sidChar)) {
            ret = sidChar;
            LocalFree(sidChar);
        } else {
            ok = false;
            cerr << "invalid SID in ConvertSidToStringSidA()\n";
        }
    }

    CloseHandle(processHandle);
    if (processToken != INVALID_HANDLE_VALUE) {
        CloseHandle(processToken);
    }

    return ret;
}
