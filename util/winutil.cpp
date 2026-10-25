// SPDX-FileCopyrightText: 2015 Andreas Hartmetz <ahartmetz@gmail.com>
// SPDX-License-Identifier: LGPL-2.1-or-later OR MPL-2.0

#include "winutil_p.h"

#include <iostream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sddl.h>
#endif

std::string fetchWindowsSid()
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
        std::cerr << "OpenProcessToken failed " << GetLastError() << '\n';
    }

    PSID psid;
    if (ok) {
        DWORD n;
        SetLastError(0);
        GetTokenInformation(processToken, TokenUser, nullptr, 0, &n);
        TOKEN_USER *token_user = static_cast<TOKEN_USER *>(alloca(n));
        // std::cerr << "GetTokenInformation to get length: length is " << n
        //           << " and GetLastError() returns " << GetLastError() << ".\n";
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || !token_user) {
            n = 0;
        }
        if (GetTokenInformation(processToken, TokenUser, token_user, n, &n)) {
            psid = token_user->User.Sid;
        } else {
            ok = false;
            std::cerr << "GetTokenInformation failed " << GetLastError() << '\n';
        }
    }

    if (ok && !IsValidSid(psid)) {
        ok = false;
        std::cerr << "IsValidSid() says no\n";
    }

    std::string ret;
    if (ok) {
        char *sidChar = nullptr;
        if (ConvertSidToStringSidA(psid, &sidChar)) {
            ret = sidChar;
            LocalFree(sidChar);
        } else {
            ok = false;
            std::cerr << "invalid SID in ConvertSidToStringSidA()\n";
        }
    }

    CloseHandle(processHandle);
    if (processToken != INVALID_HANDLE_VALUE) {
        CloseHandle(processToken);
    }

    return ret;
}
